
# Cami.js Framework Documentation

Cami.js is a lightweight framework for building reactive web applications. It combines state management, custom elements, and a data query system to provide a complete solution for modern web development.

## Conceptual Guide

This section explains the core ideas and philosophy behind Cami.js.

### Core Concepts

Cami.js is built around these fundamental concepts:

- **Stores** - Centralized state containers
- **Actions** - Functions that modify state
- **Memos** - Computed values derived from state
- **Reactive Elements** - Web Components that update automatically
- **Queries & Mutations** - Async data operations with caching

### Design Philosophy

Cami.js follows these principles:

1. **Centralized State Management**
   - Keep application state in stores, not components
   - Make state changes explicit through actions
   - Derive computed values through memos

2. **Component-Oriented Architecture**
   - Build UIs using Web Components (custom elements)
   - Keep components stateless and focused on rendering
   - Use composition for complex interfaces

3. **Unidirectional Data Flow**
   - Data flows from stores to components
   - User interactions trigger actions
   - Actions update state
   - Updated state renders components

4. **Pragmatic Reactivity**
   - Automatic updates with minimal boilerplate
   - Efficient rendering based on actual changes
   - Explicit control when needed

## Tutorials

### Getting Started with Cami.js

This tutorial will guide you through creating a basic counter application with Cami.js.

#### 1. Set up your project

```html
<!DOCTYPE html>
<html>
<head>
  <title>Cami.js Counter</title>
  <script type="module">
    import { store, ReactiveElement, html } from './cami.module.js';
  </script>
</head>
<body>
  <counter-app></counter-app>
</body>
</html>
```

#### 2. Create a store

```javascript
const counterStore = store({
  state: {
    count: 0
  },
  name: "counter-store"
});
```

#### 3. Define actions

```javascript
counterStore.defineAction("increment", ({ state, payload }) => {
  state.count += (payload?.amount || 1);
});

counterStore.defineAction("decrement", ({ state, payload }) => {
  state.count -= (payload?.amount || 1);
});

counterStore.defineAction("reset", ({ state }) => {
  state.count = 0;
});
```

#### 4. Create a custom element

```javascript
customElements.define("counter-app", class extends ReactiveElement {
  template() {
    const { count } = counterStore.getState();
    
    return html`
      <div>
        <h1>Counter: ${count}</h1>
        <button @click=${() => counterStore.dispatch("decrement")}>-</button>
        <button @click=${() => counterStore.dispatch("reset")}>Reset</button>
        <button @click=${() => counterStore.dispatch("increment")}>+</button>
      </div>
    `;
  }
});
```

### Building a Todo List

For a more complete tutorial on building a todo list application with Cami.js, see the [Todo List Tutorial](./tutorials/todo-list.md).

## How-To Guides

These guides show how to solve specific problems with Cami.js.

### Managing State with Stores

#### Creating a Store

```javascript
const myStore = store({
  state: {
    user: null,
    preferences: {
      theme: 'light',
      notifications: true
    },
    items: []
  },
  name: "app-store"
});
```

#### Persisting to Local Storage

```javascript
const storage = createLocalStorage({
  name: "app-storage",
  version: 1
});

myStore.afterHook(persistToLocalStorageThunk(storage));
```

### Working with Actions

#### Basic Actions

```javascript
myStore.defineAction("setTheme", ({ state, payload }) => {
  state.preferences.theme = payload;
});

// Usage
myStore.dispatch("setTheme", "dark");
```

#### Validating Actions with Specs

```javascript
myStore.defineSpec("addItem", {
  precondition: ({ payload }) => {
    return invariant("Item must have a name", () => {
      return payload && typeof payload.name === "string";
    });
  }
});

myStore.defineAction("addItem", ({ state, payload }) => {
  state.items.push(payload);
});
```

### Handling Asynchronous Operations

#### Defining and Using Queries

```javascript
myStore.defineQuery("fetchUser", {
  queryKey: (id) => ["user", id],
  queryFn: (id) => fetch(`/api/users/${id}`).then(r => r.json()),
  onSuccess: ({ data, dispatch }) => {
    dispatch("setUser", data);
  }
});

// Usage
await myStore.query("fetchUser", userId);
```

#### Creating Mutations

```javascript
myStore.defineMutation("updateUser", {
  mutationFn: (payload) => fetch("/api/users", {
    method: "PUT",
    body: JSON.stringify(payload)
  }),
  onSuccess: ({ invalidateQueries }) => {
    invalidateQueries(["user"]);
  }
});

// Usage
await myStore.mutate("updateUser", userData);
```

### Building Custom Elements

#### Creating a Simple Component

```javascript
customElements.define("user-profile", class extends ReactiveElement {
  template() {
    const { user } = myStore.getState();
    
    if (!user) {
      return html`<div>Loading user...</div>`;
    }
    
    return html`
      <div class="profile">
        <h2>${user.name}</h2>
        <p>${user.email}</p>
        <button @click=${() => myStore.dispatch("logoutUser")}>Logout</button>
      </div>
    `;
  }
});
```

### Error Handling

#### With Action Specs

```javascript
myStore.defineSpec("transferFunds", {
  precondition: ({ payload, state }) => {
    return invariant("Insufficient funds", () => {
      return state.account.balance >= payload.amount;
    });
  }
});
```

#### With Try/Catch

```javascript
try {
  await myStore.dispatchAsync("riskyOperation");
} catch (error) {
  myStore.dispatch("showError", error.message);
}
```

## Reference

Detailed reference documentation for all Cami.js APIs.

### Store API

#### Creation

```javascript
const myStore = store({
  state: Object,  // Initial state
  name: String    // Name for debugging
});
```

#### Methods

- `getState()` - Returns current state
- `dispatch(actionName, payload?)` - Dispatches an action
- `dispatchAsync(actionName, payload?)` - Dispatches an async action
- `defineMemo(name, fn)` - Defines a computed value
- `memo(name, ...args)` - Gets a computed value
- `defineAction(name, fn)` - Defines an action
- `defineAsyncAction(name, fn)` - Defines an async action
- `defineSpec(actionName, spec)` - Defines validation for an action
- `defineMachine(name, transitions)` - Defines a state machine
- `trigger(machine, event)` - Triggers a state machine event
- `defineQuery(name, options)` - Defines a query
- `query(name, ...args)` - Executes a query
- `defineMutation(name, options)` - Defines a mutation
- `mutate(name, ...args)` - Executes a mutation
- `invalidateQueries(queryKey)` - Invalidates cached queries
- `afterHook(fn)` - Adds a hook after state changes

### ReactiveElement API

#### Creation

```javascript
class MyElement extends ReactiveElement {
  // Lifecycle hooks
  connectedCallback() {
    super.connectedCallback();
    // Your initialization code
  }
  
  disconnectedCallback() {
    // Your cleanup code
    super.disconnectedCallback();
  }
  
  // Required template method
  template() {
    return html`<div>Content</div>`;
  }
}

customElements.define("my-element", MyElement);
```

#### Properties

All properties set on a ReactiveElement are reactive by default:

```javascript
element.prop = 'value'; // Updates template automatically
```

#### Methods

- `template()` - Returns the element's template (must be implemented)
- `update()` - Manually triggers a re-render

### Template Functions

#### html

```javascript
html`<div>${dynamicValue}</div>`
```

Supports:
- Attribute binding: `<div class=${className}>`
- Property binding: `<input .value=${value}>`
- Event binding: `<button @click=${handleClick}>`
- Conditional rendering: `${condition ? html`<span>Yes</span>` : null}`
- Iterative rendering: `${items.map(item => html`<li>${item}</li>`)}`

### Query System

#### Query Options

```javascript
{
  queryKey: Function,  // Returns a unique key array
  queryFn: Function,   // Returns a Promise
  onFetch: Function,   // Called when query starts
  onSuccess: Function, // Called on success
  onError: Function,   // Called on error
  onSettled: Function, // Called after success or error
  staleTime: Number,   // Cache duration in ms
  refetchOnWindowFocus: Boolean,
  refetchOnReconnect: Boolean
}
```

#### Mutation Options

```javascript
{
  mutationFn: Function,  // Returns a Promise
  onMutate: Function,    // Called before mutation
  onSuccess: Function,   // Called on success
  onError: Function,     // Called on error
  onSettled: Function    // Called after success or error
}
```

## Advanced Topics

### Store Organization Patterns

There are several patterns for organizing stores in your application:

#### Single Root Store

Best for small to medium applications:

```javascript
const rootStore = store({
  state: {
    user: null,
    ui: { theme: 'light' },
    data: { posts: [] }
  },
  name: "root-store"
});
```

#### Domain-Specific Stores

Better for larger applications:

```javascript
const userStore = store({
  state: { user: null, preferences: {} },
  name: "user-store"
});

const postsStore = store({
  state: { posts: [], comments: [] },
  name: "posts-store"
});

const uiStore = store({
  state: { theme: 'light', sidebar: 'closed' },
  name: "ui-store"
});
```

#### Micro-Stores

For performance-critical features:

```javascript
const realtimeStore = store({
  state: { messages: [] },
  name: "realtime-store"
});
```

### Performance Optimization

#### State Updates

Immer handles immutability efficiently:

```javascript
// Direct mutations are converted to immutable updates
store.defineAction("addItem", ({ state, payload }) => {
  state.items.push(payload);  // Efficient and clean
});
```

#### Rendering Optimization

```javascript
// Break large components into smaller ones
customElements.define("user-list", class extends ReactiveElement {
  template() {
    const { users } = userStore.getState();
    return html`
      <div>
        ${users.map(user => html`
          <user-item .user=${user}></user-item>
        `)}
      </div>
    `;
  }
});
```

#### Throttling Updates

```javascript
const throttler = new Throttler(200);

element.addEventListener('input', (e) => {
  throttler.run(() => {
    store.dispatch("updateSearch", e.target.value);
  });
});
```

### Testing

#### Testing Actions

```javascript
test("increment action adds the correct amount", () => {
  const testStore = store({
    state: { count: 0 },
    name: "test-store"
  });
  
  testStore.defineAction("increment", ({ state, payload }) => {
    state.count += payload.amount;
  });
  
  testStore.dispatch("increment", { amount: 5 });
  expect(testStore.getState().count).toBe(5);
});
```

#### Testing Components

```javascript
test("counter-element displays the correct count", async () => {
  const element = document.createElement("counter-element");
  document.body.appendChild(element);
  
  counterStore.dispatch("setCount", 42);
  await new Promise(resolve => setTimeout(resolve, 0)); // Wait for render
  
  expect(element.shadowRoot.textContent).toContain("42");
  document.body.removeChild(element);
});
```
