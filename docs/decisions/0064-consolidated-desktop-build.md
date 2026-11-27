# 0064 — One consolidated desktop build

Status: Accepted by the owner on 2026-11-24.

## Problem

Independent feature worktrees produced independent executables. The desktop shortcut pointed to a package with recent Measure changes but without the assembly Mate icons and command architecture. Switching the shortcut between these packages made accepted work appear to disappear.

## Decision

Integrate the two current accepted lines on `codex/unified-main-build`: the Measure/UI line through `codex/assembly-mate-icons-delivery` and the assembly/command line through `codex/command-architecture`. Build and test the integrated source, publish a single `TheCAD-current` desktop package, and point the desktop shortcut to that package. Future feature branches should start from the consolidated head and return to it after focused verification. Preserve older worktrees as source history; they are no longer independent desktop release targets.

The protected `main` branch is not updated by this work. Review and owner approval remain necessary before merging the integration branch into `main`, following the project rule against automatic merges.

## Visual design

The Mate icon set uses original TheCAD drawings of the geometric relationships. [SOLIDWORKS Mate icon help](https://help.solidworks.com/2026/english/SolidWorks/sldworks/r_mate_icons_featuremanager_design_tree.htm) is a behavioral and visual reference; no SolidWorks image or resource is copied.
