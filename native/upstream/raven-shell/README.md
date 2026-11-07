# Shell component

`shell.c` is Raven code, built by `scripts/provision-controls.py` into `raven-shell.dll` on the
public Windows headers. It supplies what the system libraries of the guest cannot answer on their own:

- `PageSetupDlgW` (`pagesetup.c`): paper size, orientation and margins for the default printer or the one the request names, from
  the printer's own answers through winspool;
- the common file dialogs: `GetOpenFileNameW`, `GetSaveFileNameW` and the `IFileOpenDialog` /
  `IFileSaveDialog` objects (with `IFileDialogCustomize` check boxes), all driven by one dialog
  template assembled in memory and shown by the dialog manager of the user32 component;
- shell items (`IShellItem`, `IShellItemArray`) and `SHCreateItemFromParsingName`;
- the class registry behind `CoCreateInstance`, which creates the file dialog classes.
- OLE drag and drop (`dragdrop.c`): `RegisterDragDrop`, `RevokeDragDrop` and `DoDragDrop` within the process, files dropped from the
  desktop (`CF_HDROP` through a per-thread broker window, `DragQueryFileW`/`A`, `DragQueryPoint`, `DragFinish`) and files or
  virtual files dragged out to it.
- `ExtractIconExW` and `ExtractIconW`: the icons of a program, a library or an .ico file, at the system's large and small sizes.
- UI Automation's reserved "not supported" and "mixed attribute" values, one object each for the process.
- the task allocator, through `CoGetMalloc` and the shell's `SHGetMalloc`.
- the error objects of automation (`errorinfo.c`): `CreateErrorInfo` makes one, `SetErrorInfo` hands it to the calling thread and
  `GetErrorInfo` takes it back, once.
- the free-threaded marshaler (`CoCreateFreeThreadedMarshaler`), which agile objects aggregate: it marshals interface pointers between
  the apartments of the process as the pointers themselves.
- streams over registry values (`SHOpenRegStream`, `SHOpenRegStream2`, `OpenRegStream`), written back when a writable one is released.
- the network address control that `InitNetworkAddressControl` registers (`netaddress.c`): an edit field that reads a host name or an
  IPv4 or IPv6 address with an optional port.
- item id lists and the file system namespace (`folder.c`): `SHGetDesktopFolder`, `IShellFolder`, `IEnumIDList`, the `IL*` calls,
  `SHILCreateFromPath`, and `SHOpenFolderAndSelectItems`, which opens the folder in the desktop's file manager.
- the known folder manager (`knownfolder.c`): `IKnownFolderManager` and `IKnownFolder` for the standard folders, with their
  canonical names, categories, CSIDL numbers and paths, and the lookup of the folder that holds a path.

Its exports replace the same-named exports of `comdlg32.dll`, `ole32.dll`, `shell32.dll` and `uiautomationcore.dll`
for programs that import those libraries. Setting `RAVEN_TRACE_SHELL` to a guest file path appends one line per
interface request and dialog call to that file.
