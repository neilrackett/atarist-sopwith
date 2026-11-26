# ST Sopwith

Ported to Atari ST by [Neil Rackett](https://x.com/neilrackett)

<video width="702" height="454" src="https://github.com/user-attachments/assets/69ba8bf4-9c00-4421-b54b-21a0d1f5d18c" inline muted autoplay loop>
  <img width="681" height="423" alt="image" src="https://github.com/user-attachments/assets/072ca9d9-1d77-4d8d-b5aa-72e37c50f86b" />
</video>


## About

ST Sopwith is a port of the game "Sopwith", which was originally
by BMB Compuscience Canada. The original author David L. Clark
has kindly released the source code under the GNU GPL.

When the source code for the original DOS game was released,
it revealed a colour palette and stub functions for a planned
Atari ST version that never happened. So, 40 years later, I decided
it was finally time get this port done.

Features:

- Native ST C and ASM to match original performance: the game's full
  ten moves a second on a stock 8MHz 520ST
- Support for loading custom levels
- PC speaker style music and sound
- Multiple palettes that emulate a selection of old monitors
- Medals feature
- ~~Magilink serial network multiplayer~~ Network multiplayer via STinG - _You can even play against [SDL Sopwith](https://fragglet.github.io/sdl-sopwith/)!_ 

Not got your ST online yet? Connect it to your WiFi using [MD/Net](https://neilrackett.com/atarist/md-net)!

## Download

Download the latest release from the [releases page](https://github.com/neilrackett/atarist-sopwith/releases).

## Network play

Two players can fly against each other over a network, using the
[STinG](https://github.com/th-otto/STinG) TCP/IP stack. Any network
connection STinG can use will do ([MD/Net](https://github.com/neilrackett/md-net)
on a SidecarTridge Multi-device is the easiest), and the other player
can be another ST or a computer running
[SDL Sopwith](https://github.com/fragglet/sdl-sopwith) 2.9.0.

1. Load STinG and get online. You'll need an ST with 1MB or more: STinG
   and the game don't both fit in 512KB.
2. One player chooses **N - network game**, then **L - listen for
   connection**. The screen shows the ST's address, for example
   `at 192.168.1.20 port 3847`.
3. The other player chooses **N - network game**, then **C - connect to
   remote host**, and types that address: an IP address or a host name,
   with `:port` on the end if it isn't 3847. From SDL Sopwith, run
   `sopwith -j 192.168.1.20`, or `sopwith -l` to have the ST connect to
   the PC instead.

The game uses the connecting player's options (missiles, wounded planes,
animals, big explosions and starting level). Press Ctrl-C to stop
waiting for the other player, or to quit during a game; either way,
you're returned to the title screen.

A stock ST runs the game at its full speed, ten moves a second, but
should a busy moment ever put it behind, a network game skips drawing
the odd frame rather than hold the other player back. Over the
internet, the listening player's router needs to forward TCP port 3847
to them (see the [FAQ](FAQ.md)).

## Build

The quickest way to build ST Sopwith is to install [atarist-toolkit-docker](https://github.com/sidecartridge/atarist-toolkit-docker) and run:

```sh
stcmd make
```

That builds the hand-written Atari backend, which needs nothing else. There is
a second, interchangeable one built on
[STDL](https://github.com/neilrackett/atarist-stdl), a planar-native subset of
SDL 1.2 for the Atari ST:

```sh
stcmd make BACKEND=stdl
```

STDL is a submodule, so clone with `--recurse-submodules` (or run
`git submodule update --init` in an existing clone) and it is built for you.

The STDL build needs an ST with 1MB or more to play at full speed. On a
512KB machine it runs at about a third of the speed, so a 520ST wants the
default build, which plays at full speed there.

`stcmd make floppy` builds the default game and puts it on
`build/SOPWITH.ST`, a single-sided floppy image that starts it when an ST is
switched on with the disk in the drive.

## More Info

More information about ST Sopwith is available in some of the files
included here:

- AUTHORS - People responsible for this
- COPYING - License information (GNU GPL)
- FAQ - Frequently asked questions about ST Sopwith
- NEWS - What's new, ChangeLog

You can find lots more information about the original Sopwith here:

- http://www.sopwith.org/
- https://fragglet.github.io/sdl-sopwith
- https://en.wikipedia.org/wiki/Sopwith_(video_game)

A big thanks to [Fragglet](https://github.com/fragglet/sdl-sopwith),
whos SDL port was used as a starting point.

## Feedback

If you like ST Sopwith, click the link at the top to follow me and let me know!
