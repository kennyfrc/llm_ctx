# Cami.js Language Pack

This language pack parses JavaScript and TypeScript files that use the Cami.js framework. Cami.js is a framework for building reactive web applications that combines state management, custom elements, and a data query system.

## Supported Features

This language pack extracts the following Cami.js entities:

1. **Stores** - State containers created with `store()` or `createStore()`
2. **Actions** - Functions that modify state, created with `defineAction()`
3. **Async Actions** - Asynchronous actions created with `defineAsyncAction()`
4. **Queries** - Data fetching operations created with `defineQuery()`
5. **Mutations** - Server-side data updates created with `defineMutation()`
6. **Memos** - Computed values created with `defineMemo()`
7. **Components** - Web components created with `customElements.define()` and classes that extend `ReactiveElement`

In addition, it extracts standard JavaScript/TypeScript entities:
- Functions
- Classes
- Methods

## Supported File Extensions

- `.js` - JavaScript files
- `.jsx` - JavaScript with React JSX syntax
- `.ts` - TypeScript files
- `.tsx` - TypeScript with React JSX syntax

## Requirements

This pack requires both JavaScript and TypeScript tree-sitter grammars:

```bash
# JavaScript grammar
git clone https://github.com/tree-sitter/tree-sitter-javascript

# TypeScript grammar (includes both TS and TSX parsers)
git clone https://github.com/tree-sitter/tree-sitter-typescript
```

## Building

1. Set up the grammar repositories as described above
2. Edit the `Makefile` to point to your grammar libraries if needed
3. Run `make` to build the parser

## Using

When LLM_CTX processes files with Cami.js code, it will extract the entities described above and include them in the code map. This provides context about the Cami.js architecture, stores, actions, and components in your application, enabling LLMs to better understand the code structure.

## Example

For a file containing:

```javascript
const counterStore = store({
  state: {
    count: 0
  },
  name: "counter-store"
});

counterStore.defineAction("increment", ({ state, payload }) => {
  state.count += (payload?.amount || 1);
});

customElements.define("counter-app", class extends ReactiveElement {
  template() {
    const { count } = counterStore.getState();
    
    return html`
      <div>
        <h1>Counter: ${count}</h1>
        <button @click=${() => counterStore.dispatch("increment")}>+</button>
      </div>
    `;
  }
})
```

The pack will extract:
- A store named `counterStore` with state key `count`
- An action named `increment` in the `counterStore` store
- A component named `counter-app` with a `template` method