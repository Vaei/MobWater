# Copyright (c) Jared Taylor. All Rights Reserved

"""Runs a dedicated server and two clients, and asserts they computed the same surface.

Every other check in this plugin runs on one machine, and one machine cannot fail the claim the
whole thing rests on: that the surface is a pure function of position and time, so a server and a
client handed the same instant answer exactly the same thing. That claim is only about two machines.

Two runs, and the second is the one that matters:

- **The same clock.** All three start at the same instant and step by the same amount. Any difference
  in what they record is a difference in the arithmetic itself, and there is nowhere for one to come
  from - so nothing but an exact match is accepted.

- **The clock stepped.** The three start at 0, at one loop period, and at a thousand of them. Folded,
  every one of those is the same instant, so the surfaces have to be identical again. That is what
  proves the fold: a wrap that is out by anything at all leaves a machine an hour into a session
  drawing a different sea from one that just started, which is not a bug anybody finds by playing.

Run it from a shell rather than the editor, because it launches processes:

    python mob_water_determinism.py --engine X:/UnrealEngine --project X:/Game/Game.uproject

Nothing here is a comparison against an expected file. There is no oracle for what the sea should be
at a given second, and inventing one would be checking the plugin against a copy of itself. What is
asserted is agreement, which is the only thing that has to be true.
"""

import argparse
import csv
import os
import subprocess
import sys
import time


# The three offsets, in loop periods. Zero, one, and a thousand - which is a raw clock of eighty
# hours, far past anything a session reaches, and folds to exactly the same instant as zero.
STEPPED_PERIODS = (0, 1, 1000)

# Seconds the synthetic clock advances by each frame, and how many frames are recorded.
#
# A sixty fourth rather than a sixtieth because it is exactly representable in binary. A step that is
# not means the machine starting at eighty hours rounds its own clock differently from the one
# starting at zero, and the comparison then fails on the precision of the number it was handed rather
# than on the fold it is testing. That failure is real, and it is not this test's.
STEP = 1.0 / 64.0
FRAMES = 400

# Seconds. The loop period the three are pinned to, so a project's own configuration cannot change
# the premise of the stepped run under it.
PERIOD = 300.0

MAP = '/Engine/Maps/Entry'

# How long a machine is given to boot, record and exit before it is taken as hung.
TIMEOUT = 600


def _cvars(offset):
    """The command line that turns the harness on, as config overrides rather than exec commands.

    A -ExecCmds runs after the map has loaded, which is a race against the first frame the water
    subsystem ticks. These are applied when config is read, which is not.
    """
    return [
        '-ini:Engine:[SystemSettings]:mob.Water.Determinism=1',
        '-ini:Engine:[SystemSettings]:mob.Water.DeterminismOffset=%.10g' % offset,
        '-ini:Engine:[SystemSettings]:mob.Water.DeterminismStep=%.10g' % STEP,
        '-ini:Engine:[SystemSettings]:mob.Water.DeterminismFrames=%d' % FRAMES,
        '-ini:Engine:[SystemSettings]:mob.Water.DeterminismQuit=1',
        '-ini:Engine:[/Script/MobWater.MobWaterSettings]:TimeLoopPeriod=%.10g' % PERIOD,
    ]


def _common():
    return ['-game', '-unattended', '-nopause', '-nosplash', '-nullrhi', '-NoSound', '-log']


def launch(editor, project, offset, server, port):
    """One machine. It names its own recording after its net mode and its process id.

    Told where to write rather than left to name itself would be tidier, and is not available: the
    path is handed over as a config override, and an override is parsed by splitting on colons - so a
    Windows path loses its drive letter to the parser. Discovering the files afterwards costs
    nothing, and which client is which does not matter to any assertion here.
    """
    args = [editor, project]

    if server:
        args += [MAP, '-server']
    else:
        # A URL rather than a map, so this process is a genuine NM_Client rather than a standalone
        # game pretending to be one. The recording is written whether or not the connection lands;
        # what the connection buys is that the net mode being compared is the real one.
        args += ['127.0.0.1:%d' % port]

    args += _common()
    args += ['-port=%d' % port]
    args += _cvars(offset)

    return subprocess.Popen(args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def read(path):
    """The recording, as a list of rows of floats. Frame, raw time, folded time, then the points."""
    with open(path, 'r', newline='') as handle:
        rows = list(csv.reader(handle))

    if not rows:
        return []

    return [[float(cell) for cell in row] for row in rows[1:] if row]


def compare(recordings, fold_only):
    """Every recording against the first. Returns a list of failures.

    fold_only skips the raw clock, which is the whole point of the stepped run: the three were handed
    deliberately different raw times and the assertion is that everything downstream of the fold came
    out the same anyway.
    """
    failures = []

    names = sorted(recordings)
    base_name = names[0]
    base = recordings[base_name]

    if not base:
        return ['%s recorded nothing.' % base_name]

    for name in names[1:]:
        other = recordings[name]

        if len(other) != len(base):
            failures.append('%s recorded %d frames and %s recorded %d. They cannot be compared, and a '
                            'machine that stopped early is itself the finding.'
                            % (name, len(other), base_name, len(base)))
            continue

        for index, (a, b) in enumerate(zip(base, other)):
            if len(a) != len(b):
                failures.append('%s and %s disagree about how many points there are.' % (base_name, name))
                break

            start = 2 if fold_only else 1

            for column in range(start, len(a)):
                if a[column] == b[column]:
                    continue

                label = ('raw time' if column == 1
                         else 'folded time' if column == 2
                         else 'point %d' % (column - 3))

                failures.append(
                    'frame %d, %s: %s says %.17g and %s says %.17g. The surface is not a pure '
                    'function of the instant, which is the one thing buoyancy on two machines rests '
                    'on.' % (int(a[0]), label, base_name, a[column], name, b[column]))

                if len(failures) >= 8:
                    return failures

    return failures


def moved(recordings):
    """Whether any recording actually has waves in it.

    A comparison of three flat seas agrees perfectly and proves nothing, and a flat sea is exactly
    what a machine that failed to spawn its body would record.
    """
    for rows in recordings.values():
        for row in rows:
            if any(abs(value) > 1e-3 for value in row[3:]):
                return True
    return False


def run(editor, project, out_dir, offsets, fold_only, label, expect_same=True):
    print('[MobWater] %s' % label)

    os.makedirs(out_dir, exist_ok=True)

    # Cleared first, or the previous run's files are still sitting there and the comparison is
    # between this run and the last one, which would agree.
    for name in os.listdir(out_dir):
        if name.startswith('Determinism_') and name.endswith('.csv'):
            os.remove(os.path.join(out_dir, name))

    port = 7777
    machines = []

    for index, offset in enumerate(offsets):
        name = 'Server' if index == 0 else 'Client%d' % index
        machines.append((name, launch(editor, project, offset, server=(index == 0), port=port)))

        # The server has to be up before a client tries to reach it. A client that finds nothing
        # there still records, so this is about the net mode being the real one rather than about
        # the recording happening at all.
        if index == 0:
            time.sleep(30)

    deadline = time.time() + TIMEOUT
    for name, process in machines:
        remaining = max(deadline - time.time(), 1)
        try:
            process.wait(timeout=remaining)
        except subprocess.TimeoutExpired:
            process.kill()
            print('[MobWater]   %s had to be killed after %ds.' % (name, TIMEOUT))

    recordings = {}
    failures = []

    for name in sorted(os.listdir(out_dir)):
        if name.startswith('Determinism_') and name.endswith('.csv'):
            recordings[name] = read(os.path.join(out_dir, name))
            print('[MobWater]   %s recorded %d frames' % (name, len(recordings[name])))

    if len(recordings) < len(offsets):
        return ['%d of %d machines wrote a recording. One that never reached the point of recording '
                'is itself the finding: look in Saved/Logs for the one that is missing.'
                % (len(recordings), len(offsets))]

    if not moved(recordings):
        return ['every machine recorded a flat sea, so agreement proves nothing. The body of water '
                'the harness spawns is not answering - check that /MobWater/Waves/WP_MobWater_Ocean '
                'exists.']

    differences = compare(recordings, fold_only)

    if expect_same:
        return differences

    # The control. Two machines a third of a loop apart are at genuinely different instants, so they
    # have to record genuinely different seas - and if they do not, every agreement above was an
    # agreement about nothing. Without this the harness passes just as happily on a recorder that
    # writes the same row every frame, or on a clock that ignores its offset.
    if differences:
        print('[MobWater]   they differ, as they must')
        return []

    return ['two machines a third of a loop period apart recorded exactly the same sea. Nothing here '
            'is comparing anything: either the offset is being ignored or the recording does not '
            'depend on the clock at all.']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--engine', required=True, help='the engine root')
    parser.add_argument('--project', required=True, help='the .uproject')
    parser.add_argument('--out', default=None, help='where the recordings go')
    args = parser.parse_args()

    editor = os.path.join(args.engine, 'Engine', 'Binaries', 'Win64', 'UnrealEditor-Cmd.exe')
    if not os.path.exists(editor):
        print('[MobWater] no editor at %s' % editor)
        return 1

    out_dir = args.out or os.path.join(os.path.dirname(args.project), 'Saved', 'MobWater')

    failures = []

    failures += run(editor, args.project, out_dir, (0.0, 0.0, 0.0), False,
                    'Same clock: three machines handed the same instant')

    failures += run(editor, args.project, out_dir,
                    tuple(period * PERIOD for period in STEPPED_PERIODS), True,
                    'Stepped clock: the same instant reached from 0, one loop and a thousand')

    failures += run(editor, args.project, out_dir, (0.0, PERIOD / 3.0), True,
                    'Control: two machines a third of a loop apart, which must not agree',
                    expect_same=False)

    if failures:
        for failure in failures:
            print('[MobWater] ERROR %s' % failure)
        print('[MobWater] %d check(s) failed.' % len(failures))
        return 1

    print('[MobWater] Three machines, %d frames each, two clocks eighty hours apart, every row '
          'identical - and a control that had to differ, and did.' % FRAMES)
    return 0


if __name__ == '__main__':
    sys.exit(main())
