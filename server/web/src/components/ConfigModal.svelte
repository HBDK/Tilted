<script lang="ts">
  import { createEventDispatcher, onMount } from 'svelte';
  import { brewfatherUrl, setBrewfatherUrl } from '../lib/stores';
  import { get } from 'svelte/store';

  export let open: boolean;
  const dispatch = createEventDispatcher();

  let draftUrl: string = '';

  onMount(() => {
    const current = get(brewfatherUrl);
    draftUrl = current ?? '';
  });

  function close() {
    dispatch('close');
  }

  function save() {
    const trimmed = draftUrl.trim();
    setBrewfatherUrl(trimmed.length ? trimmed : null);
    close();
  }

  function resetOverride() {
    draftUrl = '';
    setBrewfatherUrl(null);
    close();
  }
</script>

{#if open}
  <div class="fixed inset-0 z-40 flex items-center justify-center bg-black bg-opacity-50">
    <div class="bg-white rounded-lg shadow-xl w-full max-w-lg p-6">
      <h3 class="text-lg font-semibold mb-4">Settings</h3>

      <label class="block text-sm text-gray-700 mb-2">Brewfather forwarding URL (override)</label>
      <input
        type="url"
        class="w-full p-2 border border-gray-300 rounded mb-3"
        placeholder="https://tilted.example.com/api/publish"
        bind:value={draftUrl}
      />
      <div class="text-xs text-gray-500 mb-4">If set, the app will store this URL in localStorage and use it as the Brewfather forwarding endpoint for convenience (client-side override only).</div>

      <div class="flex justify-end gap-2">
        <button class="px-3 py-2 rounded bg-gray-100 border" on:click={resetOverride}>Reset</button>
        <button class="px-3 py-2 rounded bg-white border" on:click={close}>Cancel</button>
        <button class="px-3 py-2 rounded bg-blue-600 text-white" on:click={save}>Save</button>
      </div>
    </div>
  </div>
{/if}
