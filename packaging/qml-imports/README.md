# QML imports for deployment

Not part of the program. The deployment tools (windeployqt, macdeployqt,
linuxdeploy-plugin-qt) copy only the QML modules they find imported in
the QML files they are pointed at. igitt's style (`style/`) is built into
the program and loaded by name at start, so the tools never see what it
imports; `Style.qml` imports that, and Basic, which the style falls back
to for the controls it leaves out.
