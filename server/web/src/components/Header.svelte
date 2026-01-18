<script lang="ts">
  import { onMount } from 'svelte';
  import ConfigModal from './ConfigModal.svelte';

  let open = false;
  function openSettings() { open = true; }
  function closeSettings() { open = false; }
</script>

<header class="bg-blue-600 text-white p-4 shadow-md">
  <div class="container mx-auto flex items-center justify-between">
    <div>
      <h1 class="text-2xl font-bold">Tilted Hydrometer</h1>
      <p class="text-sm opacity-80">Real-time sensor data visualization</p>
    </div>

    <div>
      <button
        class="inline-flex items-center gap-2 px-3 py-2 bg-blue-500 hover:bg-blue-400 rounded text-sm shadow"
        on:click={openSettings}
        aria-label="Open settings"
      >
        <svg xmlns="http://www.w3.org/2000/svg" class="h-4 w-4" viewBox="0 0 20 20" fill="currentColor">
          <path d="M11.3 1.046a1 1 0 00-2.6 0l-.2.6a1 1 0 01-.9.69l-.65.04a1 1 0 00-.9.6l-.3.57a1 1 0 01-.73.53l-.56.12a1 1 0 00-.6.35l-.42.5a1 1 0 01-.8.36l-.58.02a1 1 0 00-.94.66L.85 8.6a1 1 0 00.11 1.02l.42.5a1 1 0 01.27.9l-.19.56a1 1 0 00.25.96l.38.38a1 1 0 01.32.98l-.06.58a1 1 0 00.66.95l.54.16a1 1 0 01.74.63l.25.54a1 1 0 00.9.6l.67.02a1 1 0 01.9.71l.19.56a1 1 0 00.96.71h2.2a1 1 0 00.96-.71l.19-.56a1 1 0 01.9-.71l.67-.02a1 1 0 00.9-.6l.25-.54a1 1 0 01.74-.63l.54-.16a1 1 0 00.66-.95l-.06-.58a1 1 0 01.32-.98l.38-.38a1 1 0 00.25-.96l-.19-.56a1 1 0 01.27-.9l.42-.5a1 1 0 00.11-1.02l-1.12-1.68a1 1 0 00-.94-.66l-.58-.02a1 1 0 01-.8-.36l-.42-.5a1 1 0 00-.6-.35l-.56-.12a1 1 0 01-.73-.53l-.3-.57a1 1 0 00-.9-.6l-.65-.04a1 1 0 01-.9-.69l-.2-.6z" />
        </svg>
        Options
      </button>
    </div>
  </div>
</header>

<ConfigModal {open} on:close={closeSettings} />