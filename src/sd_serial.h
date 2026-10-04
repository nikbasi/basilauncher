#pragma once

// Host protocol over USB CDC Serial (115200+). Paths must be under /firmware/, /sleep/, or /maps/.
//
//   BASI\n                 -> BASI OK <version>\n
//   MKDIR /firmware\n      -> OK\n | ERR ...
//   LS /firmware\n         -> one "F <size> <name>\n" or "D <name>\n" per entry, then END\n
//   PUT /firmware/x.bin N\n -> READY\n, then N raw bytes, then DONE <written>\n | ERR ...
//   GET /firmware/x.bin\n  -> SIZE N\n, then N raw bytes ('.' ACK each 1024), then DONE\n | ERR ...
//
// Call sdSerialPoll() once per loop when idle (not during flash install).

void sdSerialPoll();
