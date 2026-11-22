# FLOPPY FIGHTERS - PS3 homebrew

A floppy-ragdoll stickman brawler. You're the blue stickman with the red bandana; waves of
stickmen drop out of the sky and flop onto the arena. Punch, kick, jump, or go limp to dodge.

## Controls
| PS3 pad | Action |
|---|---|
| Left stick / D-pad | Move |
| Square | Punch (fast, small knockback) |
| Triangle | Kick (slower, big knockback, long stun) |
| Cross | Jump |
| Circle | Go limp for 0.9s - you take half damage (2s cooldown) |
| Start | Start / try again |
| PS button | Quit to XMB |

Every 3rd wave includes a big brute. You heal 25 HP between waves.

## Status - please read
* `source/game.c` (all gameplay, physics, AI, rendering) is plain C, **tested on desktop**:
  the final version ran clean for 10 long sessions (2.5-3.5 min of game time each, scripted and
  random-button play: no NaNs, no out-of-arena points, bones within ~15% of rest length), and the
  title, fight and game-over screens were checked as rendered frames. Difficulty has only been
  judged against a simple bot, not a human, so expect to retune enemy damage/speed.
* `source/ps3_main.c` (video, pad, exit handling) was **written but never compiled or run
  on a PS3** - the sandbox it was made in couldn't reach the PS3 toolchain. It only passed a
  syntax check against stand-in headers. Expect it may need small fixes against your PSL1GHT
  version; compare with the framebuffer and pad samples that ship with PSL1GHT.
* The `.pkg` has **not** been built. Run `make pkg` (below) to produce it.
* Frame rate on real hardware is unmeasured. On a desktop CPU a frame renders in ~0.6 ms; the
  PPU is far slower, so if it runs choppy, lower the resolution in `ps3_main.c` or simplify
  `draw_fighter()`.

## Build the PKG
You need the PS3 homebrew toolchain + PSL1GHT SDK (https://github.com/ps3dev/ps3toolchain).

    export PS3DEV=/usr/local/ps3dev
    export PSL1GHT=$PS3DEV
    export PATH=$PATH:$PS3DEV/bin:$PS3DEV/ppu/bin:$PS3DEV/spu/bin
    make pkg          # -> floppy-fighters.pkg

Install on a PS3 running custom firmware or HEN: put the `.pkg` on a USB stick (or FTP it) and
use *Game > Install Package Files*. The game appears in the Game column. This is a homebrew
package (fake-signed) and will not install on stock firmware.

### No toolchain? Let GitHub build it
Put this folder in a new GitHub repository (the `.github/workflows/build-pkg.yml` file must end up at
the repo root), open the **Actions** tab, choose *Build Floppy Fighters PKG* and press **Run workflow**.
When it finishes, download the `floppy-fighters-pkg` artifact. First run is slow (it compiles the whole
PS3 compiler, about an hour); later runs reuse a cached toolchain. If the build step reports a compile
error in `ps3_main.c`, that file is the untested part - send me the log and I'll fix it.

## Test on desktop (no PS3 needed)
    gcc -O2 -o test desktop/test.c source/game.c -lm
    ./test bot 60 1 shot 30     # scripted fight, dumps a frame every 30 ticks as shot_fNNNNN.ppm
    ./test fuzz 150 3           # random button mashing, reports NaNs / stretched bones
    ./test afk 120 4 over       # stand still until beaten, dumps the game-over screen
    ./test bench                # update/render timing

## Tweaking
Gameplay numbers live at the top of `source/game.c` and in `check_hits()`, `think_enemy()`,
`spawn_enemy()`: damage, knockback, stun time, enemy speed/HP, wave size, gravity.

## Files
    source/game.c, game.h   portable game core (physics, combat, AI, software renderer, font)
    source/ps3_main.c       PS3 front-end (RSX framebuffer, pad, exit)
    Makefile, sfo.xml       PSL1GHT build + package metadata (title id FLOP00001)
    ICON0.PNG               320x176 XMB icon
    desktop/test.c          desktop test harness
