# Known Issues

- Loading a file from the SD card using `:e <filename>` will immediately overwrite the current document buffer in RAM. There is currently no warning or prompt for unsaved changes before this overwrite happens.
