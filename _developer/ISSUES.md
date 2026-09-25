# SDR9700 Project Issues

This file is the durable repository-wide ledger for issues identified during
SDR9700 project work. Record findings from implementation, review, testing,
CI, documentation, packaging, release work, hardware validation, security
checks, and repository administration here so they are not lost when they are
outside the immediate task.

## Tracking Rules

- Assign each issue the next sequential `SDR-NNNN` identifier. Never reuse an
  identifier.
- Record a confirmed issue when it is identified, even when it can be resolved
  during the same task. Do not record unsupported speculation as a finding.
- Include concrete evidence, user or project impact, and the next action needed
  to make progress.
- Use one of these statuses: `open`, `investigating`, `blocked`, `deferred`, or
  `resolved`.
- Use one of these severities: `critical`, `high`, `medium`, or `low`.
- Update an existing entry instead of creating a duplicate. Link a GitHub issue
  or pull request when one exists.
- Move completed entries to **Resolved Issues**, adding the resolution and date.
  Do not delete resolved history.
- Do not include credentials, private keys, production data, personal data, or
  other sensitive evidence. Store sensitive local material only under
  `_workspace/private/` and describe it here without exposing it.

## Entry Format

```markdown
### SDR-NNNN: Concise issue title

- Status: `open`
- Severity: `medium`
- Area: Component, workflow, or platform
- Identified: YYYY-MM-DD during the activity that exposed the issue
- Evidence: Reproduction details, logs, test names, or affected paths
- Impact: User-facing or project consequence
- Next action: Specific investigation, decision, or implementation step
- Related: GitHub issue, pull request, commit, or documentation path
```

## Open Issues

### SDR-0002: CodeQL cannot read workflow metadata in the private repository

- Status: `open`
- Severity: `high`
- Area: GitHub Actions and CodeQL
- Identified: 2026-09-25 during PR #53 validation after restoring private
  repository visibility
- Evidence: All three CodeQL jobs reported `Resource not accessible by
  integration` while requesting the workflow-run API. The job permissions did
  not grant `actions: read`, which the private workflow-run API requires.
- Impact: Required CodeQL checks fail before C/C++ and Python analysis and
  during Actions result upload, preventing normal pull-request validation.
- Next action: Grant the CodeQL job `actions: read` and confirm all three
  language jobs pass in PR #53.
- Related: `.github/workflows/codeql.yml`, PR #53, CodeQL run `36188965721`

## Resolved Issues

### SDR-0001: GitHub repository visibility contradicted repository policy

- Status: `resolved`
- Severity: `high`
- Area: Repository administration
- Identified: 2026-09-25 during final verification of the `26.9.4` release
- Evidence: GitHub reported `MCAServicesGroup/sdr9700` as public while
  `AGENTS.md` requires the repository to remain private.
- Impact: Repository source and release metadata were visible contrary to the
  repository's access policy.
- Resolution: Changed the repository visibility to private and verified that
  the `v26.9.4` release, tag, and Apple Silicon DMG remained intact.
- Resolved: 2026-09-25
- Related: `AGENTS.md`, `_developer/STATUS.md`
