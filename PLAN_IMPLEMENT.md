
## Your Task

Please collaborate, discuss, align, and implement with the Navigator model on the plan, using `veda -S $VEDA_SESSION -p navigator-plan`. Navigator has no access to tool calls, therefore you need to provide extensive context through `veda sel add`. Tokens budget for the files / context is 80k tokens, please maximize it. Use `-p navigator-plan` to start, then switch to `-p navigator-chat` if you'd like to discuss further. Only use `navigator-plan` once or unless the user instructs you to do so.

### Escaping Backticks in Prompts (Critical)

**Backticks in double-quoted prompts get evaluated by bash as command substitution.** If your prompt contains code examples with backticks, they will be executed as commands:

```bash
# BAD - double quotes evaluate backticks:
veda -p navigator-plan "The function uses `console.log`"
# Results in: sh: console.log: command not found

# GOOD - use single quotes (simplest):
veda -S $VEDA_SESSION -p navigator-plan 'The function uses `console.log` to output.'

# GOOD - escape backticks in double quotes:
veda -p navigator-plan "The function uses \`console.log\`"
```

**Recommendation:** Use single quotes (`'...'`) for prompts containing backticks. If you need variable expansion, escape backticks with backslash in double quotes. 


## Session Isolation (Critical for Multi-Agent)

**Always use `-S $VEDA_SESSION`** (or set `VEDA_SESSION` env var) to isolate your selection from other concurrent agents. Each agent should have a unique session ID.

```bash
# Set session ID (stable per shell, unique per terminal)
export VEDA_SESSION="${VEDA_SESSION:-agent-$$}"
# Or pass explicitly: veda -S my-session ...
```

---

## Setting Context (Critical)

**You must run `veda sel add` before sending prompts**—this is how you provide files for Navigator/Reviewer to see. They have no other way to access code.

```bash
# Clear and build selection (session-scoped)
veda -S $VEDA_SESSION sel clear
veda -S $VEDA_SESSION sel add "src/feature/" "src/shared/utils.ts"

# Check token count
veda -S $VEDA_SESSION sel ls
```

Maximize context (up to ~80k tokens). Select generously—more context is better.

---

## Collaborating with Navigator

Use `veda -S $VEDA_SESSION -p navigator-plan` to start planning, then `veda -S $VEDA_SESSION -p navigator-chat` for follow-up discussion.

Think of Navigator as a senior engineer you're pairing with:
- Share your understanding and proposed approach
- Ask for input on tricky decisions
- Discuss tradeoffs and alternatives
- Confirm alignment before you start implementing

Example flow:
```bash
# 1. Set the context
veda -S $VEDA_SESSION sel clear
veda -S $VEDA_SESSION sel add "src/auth/" "src/api/users.ts"

# 2. Start planning conversation
veda -S $VEDA_SESSION -p navigator-plan "Here's my understanding of the task and proposed approach: [details]. What do you think?"

# 3. Continue discussion (session-scoped resume)
veda -S $VEDA_SESSION resume "What about edge case X?"
# Or switch to chat mode for back-and-forth
veda -S $VEDA_SESSION -p navigator-chat "What about edge case X?"
```

Use `veda -S $VEDA_SESSION resume` to continue the same conversation, or start fresh with a new prompt.

**Once aligned, you (the Driver) proceed to implementation.** Navigator does not implement—you do.

---

## Implementation

After aligning with Navigator:
- Execute the plan using your native editing tools
- Validate as you go (check files, search for issues)
- You can consult Navigator mid-implementation if you hit unexpected questions:
  ```bash
  veda -S $VEDA_SESSION -p navigator-chat "Quick question: should X handle Y this way?"
  ```


## Reminders

Make sure to onboard yourself with veda at `~/.pi/agent/docs/veda.md` before acting.
Key commands:
- `veda -S $VEDA_SESSION sel add` to build context (quote globs: `"src/*.c"`)
- `veda -S $VEDA_SESSION sel ls` to verify selection and token count
- `veda -S $VEDA_SESSION -p navigator-plan` for initial planning (xhigh reasoning)
- `veda -S $VEDA_SESSION -p navigator-chat` for follow-up discussion (medium reasoning)
- `veda -S $VEDA_SESSION resume` to continue a conversation (session-scoped)
- All personas run in read-only sandbox mode
- **Always use `-S $VEDA_SESSION`** to avoid conflicts with other agents
