# Windows 11 Arm screen-capture diagnosis (Issue #2)

This change instruments the desktop preflight. It changes no product code or Windows settings.

## Invariants

- Preserve the original two colors, original 20 x 50ms message-pump wait for each color, exact RGB comparison, and failing exit code.
- Save the original verdict before any late observations. Late success never promotes the run to ready.
- Additionally require the product's screen BitBlt path to match, the sentinel to be foreground before/after capture, and an active consistent input desktop.
- Read only: no desktop switching, registry changes, privacy consent dismissal, sign-in changes, token elevation, or termination of unrelated processes.
- Product tests execute only after successful preflight. A blocked environment is neither a product pass nor a product failure.

## Execution

The existing manual workflow accepts `include_windows11=true`. Pull requests from explicitly named `diagnose/desktop-*` branches also opt into the Windows 11 Arm diagnostic job. Both Server jobs remain required and unchanged in scope. No release is published for a PR.

The preflight step has a three-minute process limit; `always()` uploads its incremental evidence even when it fails. Only disposable hosted desktops are captured by default; the existing local capture opt-in remains required.

## Evidence

`desktop-readiness.json` retains the original observations and status. `desktop-diagnostic.json` is checkpointed before/after captures and includes:

- Process/native architecture, thread apartment and DPI context, DWM enablement (not proof of a current composed frame).
- For each original/late sample: timestamps, session/WTS state, station name/visibility flags, thread/input desktop names and UOI_IO, plus query errors.
- Sentinel, foreground before/after, and hit-point root HWND identity, process/session, class/title, rectangle, visibility/minimization/cloaking and DPI.
- WM_PAINT/WM_ERASEBKGND counts and paint phase (request processing, not proof of visible output).
- GetPixel's raw COLORREF, explicit CLR_INVALID handling, and RGB from the actual BitBlt bitmap at the same recorded screen coordinate.
- On failure: both accessible desktop window inventories, relevant shell/setup process session IDs, one full virtual-desktop capture, and at most ten late captures bounded by a five-second loop deadline. A blocking native call is additionally bounded by the workflow process limit.

Input desktop handles opened with DESKTOP_READOBJECTS are closed. Borrowed GetThreadDesktop/GetProcessWindowStation handles are not closed. Names/UOI_IO are compared within the same process station/session, never raw handle equality. OpenInputDesktop success alone is insufficient because it can also succeed in a disconnected session.

## Interpretation

1. Inactive/disconnected session or thread/input desktop mismatch: environment context failure.
2. Same input desktop and privacy/setup UI covering the capture point: identify the owning window/process and corroborate with the full image. Process presence alone is insufficient.
3. Sentinel pixels elsewhere in the full image: coordinate/DPI investigation.
4. Paint phase does not advance: sentinel/message processing investigation.
5. BitBlt sees the changing sentinel but GetPixel does not: capture-method discrepancy.
6. Context, coordinates and paint processing all agree but GDI output stays stale: add an independent monitor-level DXGI Desktop Duplication probe, matching adapter/output and recording HRESULT/frame timestamps. Do not label this a DWM defect solely from GDI or DwmIsCompositionEnabled.

PrintWindow/DrawToBitmap are not screen proof. DwmFlush is not a session-wide flush. Neither can replace the screen-pixel gate. Access denied or unsupported DXGI is diagnostic data, never a pass.

## Completion criteria

Diagnose the failing boundary using correlated context/window/pixel evidence. Then establish both original exact colors through GetPixel and the product's BitBlt path with consistent active input context and foreground. Finally validate current Windows 11 Notepad, Explorer, Store and Calculator in normal, maximized and restored states (12 real-app cases), reviewing screenshots and retaining the existing geometry, pixel, hover, stability and shutdown checks. Physical monitor movement and mixed DPI remain separate coverage. Issue #2 remains open until these conditions hold.
