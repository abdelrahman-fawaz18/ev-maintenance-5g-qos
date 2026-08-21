# Paired Policy Effects at Selected Loads

Each effect is computed within a matched load/seed/traffic-class pair before bootstrap resampling. Positive values favor the QoS-aware policy.

| Load [Mbps] | Class | PDR gain [percentage points] | Deadline violations avoided [percentage points] | Penalized delay reduced [ms] |
|---:|---|---:|---:|---:|
| 20 | ALERT | 0.787 [0.000, 2.186] | 4.109 [0.000, 10.109] | 147.120 [1.801, 392.044] |
| 20 | FAULT | 2.051 [0.000, 5.643] | 5.549 [0.071, 12.070] | 71.521 [1.745, 178.113] |
| 23 | ALERT | 0.999 [0.000, 2.745] | 14.000 [6.000, 24.000] | 243.735 [71.580, 477.075] |
| 23 | FAULT | 2.695 [0.208, 6.623] | 15.737 [5.999, 25.999] | 208.219 [82.781, 359.150] |
| 30 | ALERT | 5.413 [4.024, 7.496] | 100.000 [100.000, 100.000] | 1526.487 [1451.810, 1647.182] |
| 30 | FAULT | 18.927 [16.296, 22.433] | 99.998 [99.995, 100.000] | 1271.427 [1265.456, 1278.408] |
| 40 | ALERT | 11.876 [10.741, 13.373] | 100.000 [100.000, 100.000] | 1439.239 [1357.018, 1577.087] |
| 40 | FAULT | 37.469 [35.533, 39.937] | 99.999 [99.996, 100.000] | 986.642 [975.414, 1002.015] |
