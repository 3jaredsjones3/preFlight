# AD5M/AD5X thermal scheduling coupon protocol

This protocol defines evidence collection; it is not a claim that a coupon has
been printed. Register printer serial, firmware, nozzle, material lot, bed and
ambient conditions before printing. Use the same commit, machine fingerprint,
process settings and fixture geometry for both variants.

For each printer/material pair, print matched coupons with preFlight source order
and with an analysis-only proposal copied into the experimental order. Keep the
proposal sidecar and final G-code immutable. Record contact-age temperature
observations at the selected neighbor interface, destructive bond-proxy load,
visible surface/bridge/support quality, print time, failures and all exclusions.
Do not pool results across printers or material lots.

Raw observations are CSV with one row per coupon/interface and are normalized
into thermal-calibration.schema.json. A fit is accepted only when metadata is
complete, ages span the fit range, measurements show cooling, and residual and
uncertainty diagnostics are retained. The fit artifact is bound to the source
dataset hash and machine fingerprint. Physical A/B evidence must be preregistered
before a recommendation is enabled.
