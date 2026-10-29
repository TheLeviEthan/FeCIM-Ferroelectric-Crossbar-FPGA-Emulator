# sw/fecim/parameter_sets

One YAML file per published source. Each file is a complete device parameter set with its
citation and a confidence label for every value.

- **Schema:** `docs/plans and specs/06-physics-calibration/device-model.md` §4 (normative).
- **Extraction notes and source locations:** `docs/plans and specs/06-physics-calibration/literature-survey.md`.
- Store raw device quantities (volts, mV, counts, fractions), never register values.
- `null` means unreported. It forces a sweep rather than a guess.
- Composite sets set `composite: true` and name a source for every parameter.
