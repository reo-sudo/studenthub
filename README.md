# studenthub

A personal student dashboard for the Nintendo 3DS: timetable, assignments, grades, courses and links, with a night-campus top screen that shows your name, the current week, your next class and your next due date.

This is an unofficial, personal app. It is not made by or connected to any university, and it does not log in to any university system. Everything is stored in a text file on your SD card.

Requires custom firmware (Luma3DS) to run from the Homebrew Launcher.

## Controls

| Input | Action |
| --- | --- |
| D-pad | Move |
| A, or tap | Open / change |
| B | Back (from the main hub, quits the app) |
| L / R | Change day (Timetable) or filter (Assignments) |
| X | Add an assignment or grade |
| Y (press twice) | Delete the selected assignment or grade |
| START | Quit |

On Assignments, A cycles the selected item through Not started, In progress and Completed.

## Screens

- **Timetable:** classes for each day, with times and rooms.
- **Assignments:** Upcoming, In Progress and Completed, with days-left shown in red when due soon.
- **Grades:** your results with an average.
- **Courses:** one card per course taken from your timetable.
- **Links:** web addresses you list in the data file (the 3DS can't open them from here, so type them into the browser).
- **Settings:** name, degree, year and term, semester start date, accent colour and 3D depth. The 3D effect follows the console's 3D slider.

## Your data

The first time it runs, the app creates `sdmc:/3ds/studenthub/data.txt` with sample data. Replace the samples with your own, either inside the app or by editing the file on a computer. Adding classes and links is done in the file. Format:

```
name=Your Name
degree=Your degree
term=Year 1 | Semester 1
uni=AUT
unifull=Auckland University of Technology
semstart=2026-07-20
weeks=12
class|Tue|10:00|11:00|COMP500|Programming Concepts|WA220
assignment|2026-10-30|COMP504|Group Assignment|1
grade|COMP500|A-|87
link|Canvas|https://your-canvas-address
```

Assignment status is 0 (not started), 1 (in progress) or 2 (completed). Keep `|` out of names and titles, since it separates fields. Set `semstart` to the Monday of week 1 so the week counter is right.

## Build (no install needed): GitHub Actions

1. Create a new GitHub repository and upload everything in this folder, keeping `.github/workflows/build.yml`.
2. Open the Actions tab and run "Build 3DSX" (it also runs on every push).
3. Download the `studenthub` artifact and unzip it to get `studenthub.3dsx`.

## App icon

The icon is `icon.png` (48x48). The build packs it into the `.3dsx` so the Homebrew Launcher shows it. To use your own, replace `icon.png` with any 48x48 PNG and rebuild.

## Install

Copy `studenthub.3dsx` to `/3ds/` on your SD card and start it from the Homebrew Launcher.

## Status

Not yet compiled or run on a console. The code has been syntax-checked on a PC against stand-in headers only, so the real build and the first run on your 3DS are the actual tests.
