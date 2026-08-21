# Bootstrap Confidence Interval Method Note

The final analysis keeps every completed random seed. No difficult seed is removed merely because its channel or queueing outcome is unfavorable; exclusion requires a proven run-integrity failure.

For each scenario, load, traffic class, and metric, the analysis resamples the seed-level metric values 10000 times with replacement. The mean is calculated for each resample. The lower and upper bounds are the empirical tails of the resampled means for a 95% confidence interval.

For PDR and deadline violation rate, intervals are bounded to [0, 1] because these metrics are probabilities. This avoids physically impossible intervals such as PDR > 1. Delay and throughput intervals are not bounded.
