# kilo
kilo is a free simple terminal text editor written in C.

clone the repository to a local directory and build:

```
git pull https://github.com/FMJ2000/kilo.git`
cd kilo
make
```

to start a new file:

```
./kilo
```

to open an existing file:

```
./kilo <filename>
```

shortcuts:

```
Ctrl+S - save file
Ctrl+O - open file
Ctrl+Q - quit
Ctrl+F - find
Shift+Tab - switch between files
Ctrl+C - copy
Ctrl+V - paste
Ctrl+D - duplicate line
Ctrl+K - delete line
```

### version 0.0.4

- added multiple file support. Press `Ctrl+N` or `Ctrl+O` to open a new or existing file in the same workspace. Switch between files with `Shift+Tab`.

### version 0.0.3

- added copy and paste functionality

### version 0.0.2

- auto tab indent

### version 0.0.1

- the base editor constructed by completing the tutorial at [snaptoken](https://viewsourcecode.org/snaptoken/kilo/index.html).

### known bugs

copying multiple lines is not working as expected, please just use single line copy and paste. Will be fixed in a future update.
