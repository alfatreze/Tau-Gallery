# Test outcome records

`outcomes.jsonl`: one JSON line per test result (method, technique, target, evidence label, cost in minutes, outcome, findings, mutants killed, later confirmation). Format and rules: pocket-testkit `methods/effectiveness/README.md`; the tool is `tools/outcomes.py` (a copy of the kit's). The project key `p-2084f961` is anonymous: the file can be sent to the kit's library as it is after reading the free text once (the `ref` fields are audit ids of this project and mean nothing outside it).

```bash
make outcomes-report            # the table: which methods found real bugs, at what cost
make outcomes-validate
python3 tools/outcomes.py add docs/outcomes/outcomes.jsonl --project p-2084f961 --method fuzzing --technique "..." --target parser --label host --outcome bug-found \
    --finding design-bug:medium:field:"what it was" --cost setup=240,run=5,human=30 --mutation 11/11 --ref B-NNNN
python3 tools/outcomes.py confirm docs/outcomes/outcomes.jsonl o-008 --label pocket --note "what the device showed"
make verdict RECORD=1           # run the ladder and append a record per automatic rung (outcome clean, or failed until classified)
```
How to classify a finding: **design-bug** / **spec-gap** a real defect of what is built; **test-gap** a defect of the tests (found by mutation or review); **tool-artefact** / **model-fault** the alarm was the simulator's, the testbench's or the host model's fault. `would_otherwise_surface` says where the bug would have shown up without this method: never, simulation, fit, pocket, field. Costs are estimates. The first 15 records (2026-11, B-1063 to B-1079) were written afterwards from the audit trail; their costs are rough.

Read the table with care: 15 records from one project. At the time of writing the only methods that found real bugs were fuzzing (2 bugs, both found late) and a human reading of an install plan (1); simulation cost the most minutes and found only artefacts and test gaps; the formal proofs found no bug and are strong guarantees for the blocks they cover.
