# Project documentation

- `requirements/` describes the intended product and owner-approved requirements.
- `decisions/` records proposals, trade-offs, and explicit owner decisions.
- The root `AGENTS.md` defines development and delivery rules.

Long-term scope is a vision, not approval to implement individual features.
Keep proposed requirements distinguishable from approved behavior. Link a
requirement to its decision record when a major decision has been approved.

For an approved implementation task, proceed autonomously with ordinary details,
implementation, appropriate tests, fixes, a commit, and a push to a dedicated
feature branch. Report the result and limitations. Seek explicit owner approval
for major architecture or functional behavior choices, and present options and
trade-offs when requirements are unclear. Do not merge into main automatically.

## Development guides

- [Windows build and run](build-windows.md)
- [Dependencies and licenses](dependencies.md)

- [Native document formats](file-formats.md)

- [Window and part-document verification](verification/part-documents-viewport.md)
