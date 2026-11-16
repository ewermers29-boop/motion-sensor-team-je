// bench/table-bench.js: the State Table for bench pairs, in code.
//
// bench.html loads this file instead of table.js, so a bench pair's rows live
// here. Same format as table.js: signals are the numbers sense-serial.js writes
// (each 0 to 1, named in its SIGNALS), states are checked top to bottom and the
// first true row wins.
//
// hold is in seconds: how long a rule must stay true before the state changes.
// It is a tradeoff you choose per row. A short hold feels instant but flickers
// when a reading wobbles near its line; a long hold is calm but slow.
// Keep "nobody" last, so it catches everything the other rows miss.

window.TABLE = {
  // The HC-SR04 example. Replace these with your own signals and rows.
  signals: ['present', 'near'],

  states: [
    { name: 'close',  when: s => s.near > 0.85,   hold: 0.15 },
    { name: 'here',   when: s => s.present > 0.5, hold: 0.25 },
    { name: 'nobody', when: s => s.present < 0.2, hold: 0.4 },
  ],
};
