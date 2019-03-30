# CHANGELOG.md

This log file summarises the changes made to the LTFS implementation version 3.0.0 by HPE. The changes are in alphabetic order by file, and in reverse chronological order.

This implementation corresponds to LTFS as defined in the two equivalent documents:

- [ISO/IEC 20919:2016 _Information technology – Linear Tape File System (LTFS) Format Specification_](https://www.iso.org/standard/69458.html)
- [Linear Tape File System (LTFS) Format Specification. Version 2.2.0](https://www.snia.org/sites/default/files/LTFS_Format_2.2.0_Technical_Position.pdf), 2013–12–21

SNIA has also published newer versions, which are not implemented here:

- [Linear Tape File System (LTFS) Format Specification. Version 2.3.1](https://www.snia.org/sites/default/files/technical_work/LTFS/LTFS_Format_2.3.1_TechPosition.pdf), 2017–05–24
- [Linear Tape File System (LTFS) Format Specification. Version 2.4.0](https://www.snia.org/sites/default/files/technical_work/LTFS/LTFS_Format_2.4.0_TechPosition.pdf), 2017–12–01

as well as a working draft of the next version:

- [Linear Tape File System (LTFS) Format Specification. Version 2.5.0 Revision 1. Working Draft](https://www.snia.org/sites/default/files/technical_work/PublicReview/LTFS_Format_2%205%200_draft_rev_%201_190220.pdf), 2019–02–14

## [ltfs/src/tape_drivers/osx/ltotape/ltotape_supdevs.h](ltfs/src/tape_drivers/osx/ltotape/ltotape_supdevs.h)

##### 2017-03-12
- added macOS (and Linux) support for `LTO-7 HH`, `ULTRIUM-HH6` and `ULTRIUM-HH7` desks

##### 2017-03-08
- added macOS (and Linux) support for `ULTRIUM-HH5` desk
- replaced tabs by spaces for alignment

## [ltfs/src/tape_drivers/windows/ltotape/ltotape_supdevs.h](ltfs/src/tape_drivers/windows/ltotape/ltotape_supdevs.h)

##### 2017-03-12
- added Windows support for `LTO-7 HH`, `ULTRIUM-HH6` and `ULTRIUM-HH7` desks
- replaced tabs by spaces for alignment

##### 2017-03-08
- added Windows support for `ULTRIUM-HH5` desk
