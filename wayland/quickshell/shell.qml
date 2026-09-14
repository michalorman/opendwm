// opendwm bar port for Quickshell + Hyprland.
// Layout mirrors the X11 bar: workspaces left, clock center,
// recording/dictation/volume/RAM right.
// Requires Quickshell >= 0.2.1 with Hyprland and PipeWire support.
import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Hyprland
import Quickshell.Services.Pipewire

Scope {
  id: root

  // Colors (from x11/config.h)
  readonly property color colBg: "#1a1b26"
  readonly property color colFg: "#c0caf5"
  readonly property color colAccent: "#7aa2f7"
  readonly property color colRecording: "#f7768e"
  readonly property color colDim: "#565f89"
  readonly property string fontFamily: "JetBrainsMono Nerd Font"
  // X11 bar uses size=10 (points); at the configured output scale this
  // matches the old session's physical text size.
  readonly property real fontPointSize: 10

  // Nerd Font icons (same codepoints as the X11 bar)
  readonly property string iconVolume: String.fromCodePoint(0xF057E)
  readonly property string iconMuted: String.fromCodePoint(0xF0581)
  readonly property string iconRam: String.fromCodePoint(0xF035B)
  readonly property string iconRecording: String.fromCodePoint(0xF0EC3)
  readonly property string iconMic: String.fromCodePoint(0xF130)
  readonly property string iconTranscribing: String.fromCodePoint(0xF110)

  // Shared status state (one set of processes for all bars)
  property bool barsVisible: true
  // Fullscreen snapshot, replaced atomically after full validation.
  // clients: mapped, visible, actual-fullscreen clients;
  // specials: monitor id -> displayed special workspace id;
  // active: monitor id -> active NORMAL workspace id (polled, so output
  // workspace swaps cannot leave it stale like the event-driven model can).
  property var fullscreenState: ({ clients: [], specials: {}, active: {} })
  property bool layoutKnown: false
  property bool monocle: true
  property string ramText: ""
  property string recordingText: ""
  property string dictationText: ""

  readonly property string clockText: Qt.formatDateTime(clock.date, "dd MMM yyyy  HH:mm")

  readonly property string volumeText: {
    const sink = Pipewire.defaultAudioSink;
    if (!sink || !sink.audio)
      return iconVolume + " --";
    if (sink.audio.muted)
      return iconMuted + " Muted";
    return iconVolume + " " + Math.round(sink.audio.volume * 100) + "%";
  }

  // Lua config mode needs Lua dispatcher expressions; legacy mode takes
  // the plain dispatcher form. usingLua is false until the module loads.
  function switchWorkspace(id) {
    if (Hyprland.usingLua)
      Hyprland.dispatch("hl.dsp.focus({workspace=" + id + "})");
    else
      Hyprland.dispatch("workspace " + id);
  }

  // Mod+b toggles visibility through this IPC target (scripts/toggle-bar).
  IpcHandler {
    target: "bar"

    function ping(): string {
      return "opendwm-bar";
    }

    function toggle(): string {
      root.barsVisible = !root.barsVisible;
      return root.barsVisible ? "visible" : "hidden";
    }
  }

  SystemClock {
    id: clock
    precision: SystemClock.Minutes
  }

  PwObjectTracker {
    objects: [Pipewire.defaultAudioSink]
  }

  function workspaceById(id) {
    for (const ws of Hyprland.workspaces.values) {
      if (ws.id === id)
        return ws;
    }
    return null;
  }

  function occupied(ws) {
    return ws !== null && ws.toplevels.values.length > 0;
  }

  function monitorForScreen(screen) {
    for (const monitor of Hyprland.monitors.values) {
      if (monitor.name === screen.name)
        return monitor;
    }
    return null;
  }

  // Active NORMAL workspace for a monitor. Prefer the polled snapshot:
  // workspace swaps can leave the event-driven model's activeWorkspace
  // stale, and outstanding-request coalescing can drop a reconciliation.
  // The event-driven value is only the pre-first-snapshot fallback.
  function activeWorkspaceId(monitor, fallback) {
    if (monitor === null)
      return -1;
    const polled = fullscreenState.active[monitor.id];
    if (polled !== undefined)
      return polled;
    return fallback !== null ? fallback.id : -1;
  }

  function hasActualFullscreen(monitor) {
    if (monitor === null)
      return false;
    const normalId = fullscreenState.active[monitor.id] ?? -1;
    const specialId = fullscreenState.specials[monitor.id] ?? -1;
    return fullscreenState.clients.some(
      client => client.monitor === monitor.id
        && (client.workspace.id === normalId || client.workspace.id === specialId)
    );
  }

  function actualFullscreen(client) {
    // mapped and hidden are independent states; a compositor-hidden client
    // is not displayed and must not suppress the bar.
    return client.mapped === true && client.hidden !== true
      && (client.fullscreen & 2) !== 0;
  }

  // Snapshot validation: any malformed record rejects the WHOLE response,
  // so partial data never replaces the confirmed state.
  function validClientRecord(client) {
    return client !== null && typeof client === "object"
      && typeof client.mapped === "boolean"
      && typeof client.hidden === "boolean"
      && Number.isInteger(client.fullscreen)
      && Number.isInteger(client.monitor)
      && client.workspace !== null && typeof client.workspace === "object"
      && Number.isInteger(client.workspace.id);
  }

  function validMonitorRecord(monitor) {
    return monitor !== null && typeof monitor === "object"
      && Number.isInteger(monitor.id)
      && monitor.specialWorkspace !== null
      && typeof monitor.specialWorkspace === "object"
      && Number.isInteger(monitor.specialWorkspace.id)
      && monitor.activeWorkspace !== null
      && typeof monitor.activeWorkspace === "object"
      && Number.isInteger(monitor.activeWorkspace.id);
  }

  // hasFullscreen includes maximization. Query the internal mode instead:
  // bit 1 is maximized, bit 2 is actual fullscreen (including combined mode 3).
  // Monitors are queried too: their activeWorkspace is the NORMAL workspace,
  // while a displayed special workspace is tracked separately.
  Process {
    id: fullscreenProc
    // Client JSON can exceed the per-argument exec limit in crowded
    // sessions, so it is piped through stdin, never passed via --argjson.
    // Both query statuses are checked explicitly, and the success sentinel
    // is printed only when jq itself succeeds: the collector publishes
    // nothing without it, since stdout closes on failed processes too.
    command: ["sh", "-c",
      "m=$(timeout -k 0.2s 2s hyprctl -j monitors) || exit 1; " +
      "c=$(timeout -k 0.2s 2s hyprctl -j clients) || exit 1; " +
      "printf '%s' \"$c\" | jq --argjson m \"$m\" '{clients: ., monitors: $m}'" +
      " && echo __OPENDWM_OK__"]
    stdout: StdioCollector {
      onStreamFinished: {
        try {
          const marker = "__OPENDWM_OK__\n";
          if (!this.text.endsWith(marker))
            return;
          const data = JSON.parse(this.text.slice(0, -marker.length));
          if (!Array.isArray(data.clients) || !Array.isArray(data.monitors))
            return;
          // Validate every record before publishing, so malformed data
          // never partially replaces the confirmed snapshot.
          const clients = [];
          for (const client of data.clients) {
            if (!root.validClientRecord(client))
              return;
            if (root.actualFullscreen(client))
              clients.push(client);
          }
          const specials = {};
          const active = {};
          for (const monitor of data.monitors) {
            if (!root.validMonitorRecord(monitor))
              return;
            if (monitor.specialWorkspace.id !== 0)
              specials[monitor.id] = monitor.specialWorkspace.id;
            active[monitor.id] = monitor.activeWorkspace.id;
          }
          root.fullscreenState = { clients: clients, specials: specials, active: active };
        } catch (error) {
          // Keep the last confirmed state until IPC recovers.
        }
      }
    }
    onRunningChanged: {
      if (!running && fullscreenRefresh.pending)
        fullscreenRefresh.restart();
    }
  }

  Timer {
    id: fullscreenRefresh
    property bool pending: false
    interval: 25
    onTriggered: {
      if (!fullscreenProc.running) {
        pending = false;
        fullscreenProc.running = true;
      }
    }
  }

  Connections {
    target: Hyprland
    function onRawEvent(event) {
      if (["fullscreen", "openwindow", "closewindow", "movewindowv2",
           "workspacev2", "activespecial", "activespecialv2",
           "monitoradded", "monitorremoved"].indexOf(event.name) >= 0) {
        fullscreenRefresh.pending = true;
        fullscreenRefresh.restart();
      }
      // Workspace moves/swaps between outputs can leave the event-driven
      // monitor model's activeWorkspace stale; reconcile it so the panel's
      // workspace highlight stays correct.
      if (["moveworkspacev2", "workspacev2"].indexOf(event.name) >= 0)
        Hyprland.refreshMonitors();
    }
  }

  Timer {
    interval: 2000
    running: true
    repeat: true
    triggeredOnStart: true
    onTriggered: {
      if (!fullscreenProc.running)
        fullscreenProc.running = true;
    }
  }

  // --- status processes -------------------------------------------------

  // Hyprland emits no event for layout changes, so poll at a short interval
  // only to choose the panel background. Keep it opaque until confirmed.
  Process {
    id: layoutProc
    command: ["hyprctl", "getoption", "general:layout"]
    stdout: StdioCollector {
      onStreamFinished: {
        if (this.text.indexOf("monocle") >= 0) {
          root.monocle = true;
          root.layoutKnown = true;
        } else if (this.text.indexOf("master") >= 0) {
          root.monocle = false;
          root.layoutKnown = true;
        }
      }
    }
  }

  Timer {
    interval: 250
    running: true
    repeat: true
    triggeredOnStart: true
    onTriggered: {
      if (!layoutProc.running)
        layoutProc.running = true;
    }
  }

  // Used RAM from /proc/meminfo (same accounting as the C bar)
  Process {
    id: ramProc
    command: ["sh", "-c", "awk '/MemTotal/{t=$2}/MemAvailable/{a=$2}END{printf \"%.2f GB\", (t-a)/1048576}' /proc/meminfo"]
    stdout: StdioCollector {
      onStreamFinished: {
        const t = this.text.trim();
        if (/^\d+(\.\d+)? GB$/.test(t))
          root.ramText = t;
      }
    }
  }

  Timer {
    interval: 60000
    running: true
    repeat: true
    triggeredOnStart: true
    onTriggered: {
      if (!ramProc.running)
        ramProc.running = true;
    }
  }

  // Recording status follower (long-running, line-based).
  // onRunningChanged covers both clean exits and failed starts; whenever
  // the follower stops, its state becomes unknown until it reports again.
  Process {
    id: recProc
    command: ["record-menu", "status"]
    running: true
    stdout: SplitParser {
      onRead: line => root.recordingText = line.indexOf("recording") >= 0 ? "rec" : ""
    }
    onRunningChanged: {
      if (!running) {
        root.recordingText = "";
        recRestart.restart();
      }
    }
  }

  Timer {
    id: recRestart
    interval: 3000
    onTriggered: {
      if (!recProc.running)
        recProc.running = true;
    }
  }

  // Dictation status follower (same lifecycle as the recording follower)
  Process {
    id: dictProc
    command: ["voxtype", "status", "--follow", "--format", "text"]
    running: true
    stdout: SplitParser {
      onRead: line => {
        if (line.indexOf("recording") >= 0)
          root.dictationText = "mic";
        else if (line.indexOf("transcribing") >= 0)
          root.dictationText = "trn";
        else
          root.dictationText = "";
      }
    }
    onRunningChanged: {
      if (!running) {
        root.dictationText = "";
        dictRestart.restart();
      }
    }
  }

  Timer {
    id: dictRestart
    interval: 3000
    onTriggered: {
      if (!dictProc.running)
        dictProc.running = true;
    }
  }

  // --- bars (one per output) --------------------------------------------

  Variants {
    model: Quickshell.screens

    PanelWindow {
      id: bar
      required property var modelData
      screen: modelData

      // Hyprland monitor backing this output; null during early startup
      readonly property var hlMonitor: root.monitorForScreen(modelData)
      readonly property var activeWs: hlMonitor ? hlMonitor.activeWorkspace : null
      // Highlight follows the polled snapshot (recovers from stale event
      // state), falling back to the event model before the first snapshot.
      readonly property int activeWsId: root.activeWorkspaceId(hlMonitor, activeWs)

      anchors {
        top: true
        left: true
        right: true
      }
      // Bar height from actual font metrics, not a copied pixel constant
      implicitHeight: Math.ceil(barFontMetrics.height) + 6
      color: root.layoutKnown && !root.monocle ? "transparent" : root.colBg
      Behavior on color {
        ColorAnimation {
          duration: 300
          easing.type: Easing.InOutQuad
        }
      }

      FontMetrics {
        id: barFontMetrics
        font.family: root.fontFamily
        font.pointSize: root.fontPointSize
      }

      // Hide when toggled off or when this output's workspace has a
      // fullscreen window (dwm behavior, per output)
      visible: root.barsVisible && !root.hasActualFullscreen(hlMonitor)

      // Left: workspaces 1-9, 0
      Row {
        id: leftBlock
        anchors.left: parent.left
        anchors.leftMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        spacing: 0

        Repeater {
          model: 10

          Rectangle {
            property int wsId: index + 1
            property var ws: root.workspaceById(wsId)
            property bool wsOccupied: root.occupied(ws)
            property bool active: bar.activeWsId === wsId

            implicitWidth: bar.implicitHeight
            implicitHeight: bar.implicitHeight
            color: "transparent"

            Text {
              id: label
              anchors.centerIn: parent
              text: wsId === 10 ? "0" : wsId
              color: wsOccupied ? root.colFg : root.colDim
              font.family: root.fontFamily
              font.pointSize: root.fontPointSize
            }

            Rectangle {
              anchors.bottom: parent.bottom
              anchors.bottomMargin: 3
              anchors.horizontalCenter: parent.horizontalCenter
              width: label.implicitWidth + 4
              height: 2
              radius: 1
              color: root.colAccent
              visible: active
            }

            MouseArea {
              anchors.fill: parent
              onClicked: root.switchWorkspace(wsId)
            }
          }
        }

      }

      // Center: clock
      Text {
        anchors.centerIn: parent
        text: root.clockText
        color: root.colFg
        font.family: root.fontFamily
        font.pointSize: root.fontPointSize
      }

      // Right: recording, dictation, volume, RAM (icons like the X11 bar)
      Row {
        anchors.right: parent.right
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        spacing: 18

        Text {
          text: root.iconRecording
          color: root.recordingText !== "" ? root.colRecording : root.colDim
          font.family: root.fontFamily
          font.pointSize: root.fontPointSize
        }

        Text {
          text: root.dictationText === "trn" ? root.iconTranscribing : root.iconMic
          color: root.dictationText === "" ? root.colDim : root.colAccent
          font.family: root.fontFamily
          font.pointSize: root.fontPointSize
        }

        Text {
          text: root.volumeText
          color: root.colFg
          font.family: root.fontFamily
          font.pointSize: root.fontPointSize
        }

        Text {
          text: root.iconRam + " " + root.ramText
          color: root.colFg
          font.family: root.fontFamily
          font.pointSize: root.fontPointSize
        }
      }
    }
  }
}
