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

No open issues are currently recorded.

## Resolved Issues

No resolved issues are currently recorded.
