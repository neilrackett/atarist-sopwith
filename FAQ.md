ST Sopwith FAQ
===============

- **Q**: What is this? What is Sopwith?

  **A**: Sopwith is a classic side scrolling shoot 'em up developed in
  the 1980s by the Canadian firm BMB Compuscience. You pilot a *Sopwith
  Camel* biplane, attempting to bomb enemy targets while avoiding enemy
  planes. The original game was for MS-DOS.

  In 2000, Dave L. Clark, the original programmer behind Sopwith,
  released the source code to Sopwith. This is an updated version based
  on his released sources; it is not a rewrite or recreation. It is
  named ST Sopwith as it's the first version to run on the Atari ST, 
  finally implementing the port that the source code indicates was planned
  nearly 40 years ago.

- **Q**: What is an Atari ST?

  **A**: An Atari ST is a 16/32 bit computer released by Atari in the
  mid 1980s. It was a popular computer in Europe and was used for
  business and education as well as for games.

- **Q**: This is different to the original Sopwith!

  **A**: If you played one of the original DOS versions of Sopwith, you
  may notice some differences, depending on the version you played:

  - If you played what is known by fans as "Sopwith 1", you might
    remember the ground being drawn as a solid mass instead of . You
    might also notice that there are oxen and flocks of birds which were
    not present in this version. These were added in "Sopwith 2". The
    musical theme was also changed.
  - If you played what is known by fans as "Sopwith 2", you might notice
    that there are new features such as wounded planes and "splats" that
    appear on the screen. These were added in the "Author's Edition"
    release made by Sopwith's original author, Dave Clark.

  In general, ST Sopwith tries hard to avoid making any large changes
  to the original gameplay. The default settings emulate the behavior
  of "Sopwith 2" but it can be reconfigured to behave like "Sopwith 1"
  if that's what you prefer.

- **Q**: Does it have sound?

  **A**: Yes! Sound is done through an emulation of the PC Speaker
  using the digital output available on modern sound cards.

- **Q**: Does it have multiplayer support?

  **A**: No, but please feel free to add it!

- **Q**: How do I play this through a firewall?

  **A**: Most home routers require you to set up port forwarding to
  establish a connection for multiplayer. You'll need to forward TCP
  port 3847.

  Alternatively, if you can run a TCP server that forwards data between
  clients that connect to it, two Sopwith players that connect to this
  server should automatically find each other. It is fairly simple to
  write do this with "netcat". for example:

```shell
  nc -l -p 3847 -c "nc -l -p 3847"
```

- **Q**: What license is this released under?

  **A**: This is released under the GNU General Public License,
  version 2. The Sopwith source was originally released under a more
  restrictive license, but it has since been relicensed.

