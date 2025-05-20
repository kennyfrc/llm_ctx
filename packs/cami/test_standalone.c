#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Include the implementation directly
#include "cami_pack.c"

int main() {
    // Sample Cami.js code
    const char *source = "const userStore = store({\n"
                         "  state: {\n"
                         "    user: null,\n"
                         "    isLoggedIn: false\n"
                         "  },\n"
                         "  name: \"user-store\"\n"
                         "});\n"
                         "\n"
                         "userStore.defineAction(\"setUser\", ({ state, payload }) => {\n"
                         "  state.user = payload;\n"
                         "  state.isLoggedIn = !!payload;\n"
                         "});\n"
                         "\n"
                         "userStore.defineAsyncAction(\"fetchUser\", async ({ state, dispatch }) => {\n"
                         "  const user = await fetch('/api/user').then(r => r.json());\n"
                         "  dispatch(\"setUser\", user);\n"
                         "});\n"
                         "\n"
                         "customElements.define(\"user-profile\", class extends ReactiveElement {\n"
                         "  template() {\n"
                         "    const { user } = userStore.getState();\n"
                         "    return html`<div>${user.name}</div>`;\n"
                         "  }\n"
                         "});\n";
    
    // Create temporary file and arena
    CodemapFile file = {0};
    strcpy(file.path, "test.js");
    
    // Allocate memory for arena (allocation happens within parse_file for entries)
    Arena arena = {0};
    arena.base = malloc(1024 * 1024);  // 1MB arena
    arena.size = 1024 * 1024;
    arena.pos = 0;
    
    // Initialize and parse
    initialize();
    if (parse_file(file.path, source, strlen(source), &file, &arena)) {
        printf("✅ Parsing succeeded!\n");
        printf("Found %zu code entities:\n\n", file.entry_count);
        
        // Print each entity
        for (size_t i = 0; i < file.entry_count; i++) {
            CodemapEntry *entry = &file.entries[i];
            
            // Print type
            const char *kind_str = "Unknown";
            switch(entry->kind) {
                case CM_STORE: kind_str = "Store"; break;
                case CM_ACTION: kind_str = "Action"; break;
                case CM_ASYNC_ACTION: kind_str = "AsyncAction"; break;
                case CM_QUERY: kind_str = "Query"; break;
                case CM_MUTATION: kind_str = "Mutation"; break;
                case CM_MEMO: kind_str = "Memo"; break;
                case CM_COMPONENT: kind_str = "Component"; break;
                case CM_FUNCTION: kind_str = "Function"; break;
                case CM_CLASS: kind_str = "Class"; break;
                case CM_METHOD: kind_str = "Method"; break;
                case CM_TYPE: kind_str = "Type"; break;
            }
            
            printf("%s: %s", kind_str, entry->name);
            
            // Print container if applicable
            if (entry->container[0] != '\0') {
                printf(" (in %s)", entry->container);
            }
            
            // Print signature if applicable
            if (entry->signature[0] != '\0') {
                printf(" %s", entry->signature);
            }
            
            printf("\n");
        }
    } else {
        printf("❌ Parsing failed!\n");
    }
    
    // Clean up
    cleanup();
    free(arena.base);
    
    return 0;
}