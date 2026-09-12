---
name: feedback-capture-budget
description: Do not recommend bumping AUTOSAVE_PARAMETER_GETS_PER_WRITE or adjusting capture timing unless the user explicitly asks to work on it.
metadata: 
  node_type: memory
  type: feedback
  originSessionId: 5eded3cf-d0d9-41ce-86df-08d927b2c160
  modified: 2026-08-26T09:46:23.496Z
---

Do not suggest increasing AUTOSAVE_PARAMETER_GETS_PER_WRITE or adjusting capture budget/timing as a fix or stopgap. The user considers the current budget fine and will initiate capture timing work when ready.

**Why:** User explicitly said to stop recommending it — it's not the axis they want to optimize right now. The per-section CRC format redesign is the chosen path for write performance.

**How to apply:** When discussing autosave writer performance, focus on the section-based CRC format design, not capture budget knobs. Only revisit budget if the user brings it up.
