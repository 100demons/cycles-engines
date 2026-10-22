# Third-party notices

This file lists outside code, designs and ideas that these engines build on, with their licenses.
Every engine's own README repeats the credits that apply to it.

Add one entry per source, in this shape:

```
## <Name of project or person>
- Used by: <engine id(s)>
- What was used: <code copied / algorithm reimplemented / idea only>
- License: <MIT / GPL-3.0 / etc., with a link to the license text>
- Link: <URL>
```

Important: code under a "copyleft" license (for example GPL) can't simply be re-published under MIT.
If an engine ports code from another project, check that project's license **before** publishing.

## Sonic Potions LXR and LXR02Open
- Used by: `lixr`
- What was used: the LXR drum voice as a **specification and listening reference** only. The engine is
  new fixed-point code; no source line, table or sample is copied.
- License: the LXR source is released under a non-commercial license, which is why no code is used.
  LXR02Open (Brendan Clarke) is the open port that was read.
- Link: <https://github.com/SonicPotions/LXR>, <https://github.com/brendanclarke/LXR02Open>

---

Elektron, Model:Cycles, Model:Samples and Syntakt are trademarks of Elektron Music Machines MAV AB.
This project is independent and not affiliated with or endorsed by Elektron.
