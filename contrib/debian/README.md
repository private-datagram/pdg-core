
Debian
====================
This directory contains files used to package pdgd/pdg-qt
for Debian-based Linux systems. If you compile pdgd/pdg-qt yourself, there are some useful files here.

## pdg: URI support ##


pdg-qt.desktop  (Gnome / Open Desktop)
To install:

	sudo desktop-file-install pdg-qt.desktop
	sudo update-desktop-database

If you build yourself, you will either need to modify the paths in
the .desktop file or copy or symlink your pdgqt binary to `/usr/bin`
and the `../../share/pixmaps/pdg128.png` to `/usr/share/pixmaps`

pdg-qt.protocol (KDE)

