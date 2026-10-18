# State Machines

**Last Updated:** 2026-10-18 (Animate preview dialog; Animate output keeps a project)

## 1. Capture Overlay

**Source:** src/widgets/capture/capturewidget.cpp:302

### States
| State | Meaning |
|-------|---------|
| selecting | Full-screen overlay shown, user choosing a region. |
| editing | Region chosen, annotation toolbar shown. |
| accepted | User confirmed; capture exported when the overlay is destroyed. In captureFirst mode a toast follows the save. |
| cancelled | Overlay closed without a capture. |

### Transitions
| From | To | Trigger | Guard |
|------|----|---------|-------|
| selecting | editing | Mouse release on a region, or click on a highlighted window | ACCEPT_ON_SELECT not set |
| selecting | accepted | Mouse release on a region, or click on a highlighted window | ACCEPT_ON_SELECT set (CLI flag, or captureFirst hotkey/tray request) |
| editing | accepted | Save / copy / pin action | Selection not empty |
| selecting | cancelled | Escape or right click | none |
| editing | cancelled | Escape | none |

### Diagram
```mermaid
stateDiagram-v2
  [*] --> selecting
  selecting --> editing: release
  selecting --> accepted: release (accept-on-select)
  editing --> accepted: save/copy/pin
  selecting --> cancelled: esc
  editing --> cancelled: esc
  accepted --> [*]
  cancelled --> [*]
```

## 2. Screen Recording

**Source:** `src/core/recordingstate.h` (`RecState`, `canTransition`), driven by `src/core/recordingcontroller.cpp`.

### States
| State | Meaning |
|-------|---------|
| Idle | No recording in progress. |
| Selecting | Area picker overlay open. |
| Starting | Area chosen, control window shown, stream starting. |
| Recording | Frames are being written to the temp MP4. |
| Stopping | Waiting for the native recorder to finalise the file. |
| Converting | GIF only: temp MP4 is being turned into a GIF. |
| Done | File saved; history entry and toast follow. GIF: the temp MP4 is kept and handed to the toast as the edit source (see Recording Editor). |
| Failed | Start, record, stop or conversion failed. |
| Cancelled | User cancelled while recording; temp file discarded. |

### Transitions
| From | To | Trigger | Guard |
|------|----|---------|-------|
| Idle | Selecting | Record hotkey or tray item | none |
| Selecting | Starting | Area picked | none |
| Selecting | Cancelled | Cancel | none |
| Selecting | Idle | Overlay closed with no area | none |
| Starting | Recording | Native stream started | none |
| Starting | Failed | Stream failed to start | none |
| Starting | Cancelled | Cancel during the start delay | none |
| Recording | Stopping | Stop button or same hotkey | none |
| Recording | Cancelled | Cancel button | none |
| Recording | Failed | Native error mid-recording | none |
| Stopping | Converting | File finalised | GIF mode |
| Stopping | Done | File finalised | MP4 mode |
| Stopping | Failed | Finalise error or timeout | none |
| Converting | Done | GIF written | none |
| Converting | Failed | Conversion error | none |
| Done, Failed, Cancelled | Idle | Cleanup finished | none |

### Diagram
```mermaid
stateDiagram-v2
  [*] --> Idle
  Idle --> Selecting
  Selecting --> Starting
  Selecting --> Cancelled
  Selecting --> Idle
  Starting --> Recording
  Starting --> Failed
  Starting --> Cancelled
  Recording --> Stopping
  Recording --> Cancelled
  Recording --> Failed
  Stopping --> Converting
  Stopping --> Done
  Stopping --> Failed
  Converting --> Done
  Converting --> Failed
  Done --> Idle
  Failed --> Idle
  Cancelled --> Idle
```

## 3. Capture Timer

**Source:** `src/core/capturedelay.h` (`CountdownState`, `CountdownStateMachine`), driven by `Flameshot::guiWithTimer` in `src/core/flameshot.cpp`.

### States
| State | Meaning |
|-------|---------|
| Idle | No countdown running. |
| Counting | Countdown shown, waiting for the configured seconds. |
| Capturing | Countdown removed, capture overlay opening. |
| Cancelled | User clicked the countdown. |

### Transitions
| From | To | Trigger | Guard |
|------|----|---------|-------|
| Idle | Counting | Hotkey or tray region capture | `captureDelaySec` > 0 and request has no delay of its own |
| Counting | Capturing | Countdown reaches zero | none |
| Counting | Cancelled | Click on the countdown | none |
| Capturing | Idle | Overlay opened | none |
| Cancelled | Idle | Countdown closed | none |

A second hotkey press while Counting is ignored.

### Diagram
```mermaid
stateDiagram-v2
  [*] --> Idle
  Idle --> Counting
  Counting --> Capturing
  Counting --> Cancelled
  Capturing --> Idle
  Cancelled --> Idle
```

## 4. Recording Editor

**Source:** `src/core/recordingedit.h` (`EditState`, `canEditTransition`, `EditStateMachine`), driven by `src/widgets/recordingeditor.cpp`. Opened by Edit on a recording toast.

### States
| State | Meaning |
|-------|---------|
| Editing | Preview, trim handles, Beautify and Export are live. |
| Exporting | Trimmed (and beautified) MP4 is being written. |
| Converting | GIF export only: the exported MP4 is being turned into a GIF. |
| Saved | New file written; history entry and toast follow. |
| Failed | Export or conversion failed; the message is shown. |
| Closed | Window gone; a temp edit source has been removed. |

### Transitions
| From | To | Trigger | Guard |
|------|----|---------|-------|
| Editing | Exporting | Export MP4 or Export GIF | none |
| Editing | Closed | Cancel or window closed | none |
| Exporting | Converting | MP4 written | GIF export |
| Exporting | Saved | MP4 written | MP4 export |
| Exporting | Failed | Export error | none |
| Converting | Saved | GIF written | none |
| Converting | Failed | Conversion error | none |
| Failed | Editing | Error shown | none |
| Failed | Closed | Window closed | none |
| Saved | Closed | Window closes itself | none |

Closing the window while Exporting or Converting is refused. The edit source is the saved MP4 itself, or for a GIF the kept temp MP4; the toast owns a temp source until Edit hands it to the editor, and whichever holds it last removes it.

### Diagram
```mermaid
stateDiagram-v2
  [*] --> Editing
  Editing --> Exporting
  Editing --> Closed
  Exporting --> Converting
  Exporting --> Saved
  Exporting --> Failed
  Converting --> Saved
  Converting --> Failed
  Failed --> Editing
  Failed --> Closed
  Saved --> Closed
  Closed --> [*]
```

## 5. Scrolling Capture

**Source:** `src/core/scrollcapturestate.h` (`ScrollState`, `ScrollMode`, `canTransition`, `ScrollCaptureMachine`), driven by `src/core/scrollcapturecontroller.cpp`.

### States
| State | Meaning |
|-------|---------|
| Idle | No scrolling capture in progress. |
| Picking | Area picker overlay open. |
| Scrolling | Floating bar shown; the area is grabbed and stitched. Mode Manual (the user scrolls, about nine grabs a second) or Auto (synthetic wheel events, one grab after each). Every capture starts Manual. |
| Done | Tall image handed to the normal export path (TAKE_SCREENSHOT actions). |
| Cancelled | Cancel pressed; nothing is kept. |
| Failed | No area, the grab could not start, three grabs in a row failed, or a grab hung for 3 s. |

### Transitions
| From | To | Trigger | Guard |
|------|----|---------|-------|
| Idle | Picking | Scrolling capture hotkey or tray item | none |
| Picking | Scrolling | Area picked | area not empty |
| Picking | Failed | Empty area picked | none |
| Picking | Idle | Overlay closed with no area | none |
| Scrolling | Done | Done button, same hotkey, or Auto reached the end or the height limit | something was stitched |
| Scrolling | Failed | Grab error or timeout, or Done with nothing stitched | none |
| Scrolling | Cancelled | Cancel button | none |
| Done, Failed, Cancelled | Idle | Cleanup finished | none |

Mode changes happen only inside Scrolling: Manual to Auto via the Auto button when Accessibility is granted (otherwise the system prompt is shown and the mode stays Manual); Auto to Manual via Stop auto, a frame that matches nothing, or a scroll event that could not be posted.

### Diagram
```mermaid
stateDiagram-v2
  [*] --> Idle
  Idle --> Picking
  Picking --> Scrolling
  Picking --> Failed
  Picking --> Idle
  state Scrolling {
    [*] --> Manual
    Manual --> Auto
    Auto --> Manual
  }
  Scrolling --> Done
  Scrolling --> Failed
  Scrolling --> Cancelled
  Done --> Idle
  Failed --> Idle
  Cancelled --> Idle
```

## 6. Animated Background Export

**Source:** `src/core/animexportjob.h` (`AnimExportJob::State`), driven by `src/core/animexportjob.cpp` (`run`, `finish`); encoding in `src/platform/macos/animexport.mm`. Started by the Animate button.

### States
| State | Meaning |
|-------|---------|
| Rendering | Output names reserved; each frame is rendered and handed to the encoder (MP4, then GIF) on a worker thread; a progress pill shows the percentage. |
| Saved | Every requested file written; each gets its own capture project (base, annotations, Animate and effect settings), a history entry and a toast with Edit, and the first file's URL is copied. |
| Failed | An encoder or file error; partial and placeholder files are removed and the error is logged. |

### Transitions
| From | To | Trigger | Guard |
|------|----|---------|-------|
| Rendering | Saved | Worker reports success | all requested files finished |
| Rendering | Failed | Worker reports an error | none |

Rendering and encoding run together frame by frame, so there is no separate encoding state. The job cannot be cancelled; it deletes itself after Saved or Failed.

### Diagram
```mermaid
stateDiagram-v2
  [*] --> Rendering
  Rendering --> Saved
  Rendering --> Failed
  Saved --> [*]
  Failed --> [*]
```

## 7. Animate Preview

**Source:** `src/widgets/animpreviewdialog.cpp` (`AnimPreviewDialog`), opened by `CaptureWidget::handleAnimateRequest` and finished by `CaptureWidget::exportAnimation` in `src/widgets/capture/capturewidget.cpp`.

### States
| State | Meaning |
|-------|---------|
| Playing | Window-modal dialog over the editor or overlay; the loop plays in real time from the export's own frames, drawn smaller. |
| Paused | The same, holding one frame. |
| Exported | Export pressed: the style, motion and format become the saved choice and an Animated Background Export starts (the overlay first finishes its capture). |
| Cancelled | Cancel, Esc or close: nothing is written and the editor or overlay is exactly as before. |

### Transitions
| From | To | Trigger | Guard |
|------|----|---------|-------|
| Playing | Paused | Pause button | none |
| Paused | Playing | Play button | none |
| Playing | Playing | Style, motion or format changed | frames rebuilt at the same phase |
| Paused | Paused | Style, motion or format changed | frames rebuilt at the same phase |
| Playing | Exported | Export button | none |
| Paused | Exported | Export button | none |
| Playing | Cancelled | Cancel, Esc or window close | none |
| Paused | Cancelled | Cancel, Esc or window close | none |

The dialog opens Playing, preset from the reopened output's Animate settings when the editor came from an Animate toast or history entry, otherwise from the saved choice. It deletes itself on close.

### Diagram
```mermaid
stateDiagram-v2
  [*] --> Playing
  Playing --> Paused
  Paused --> Playing
  Playing --> Exported
  Paused --> Exported
  Playing --> Cancelled
  Paused --> Cancelled
  Exported --> [*]
  Cancelled --> [*]
```
