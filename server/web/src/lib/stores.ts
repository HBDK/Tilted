import { writable } from 'svelte/store';
import type { SensorData } from './types';

export const selectedSensorId = writable<string | null>(null);
export const sensorData = writable<SensorData | null>(null);
export const loading = writable<boolean>(false);
export const error = writable<string | null>(null);

// NEW: Store for selected date range (Unix timestamps in milliseconds)
export const selectedDateRange = writable<{ startTime: number; endTime: number }>({
  startTime: new Date(Date.now() - 24 * 60 * 60 * 1000).getTime(), // Default: 24 hours ago
  endTime: new Date().getTime(), // Default: Now
});

// Brewfather URL override (persisted to localStorage)
// Brewfather URL override (server-backed)
export const brewfatherUrl = writable<string | null>(null);

// Load the stored brewfather override from the server
export async function loadBrewfatherUrl(): Promise<void> {
  try {
    const res = await fetch('/api/settings/brewfather');
    if (!res.ok) {
      console.error('Failed to load brewfather setting', res.status);
      return;
    }
    const body = await res.json();
    // body.stored may be null or a string
    if (body && Object.prototype.hasOwnProperty.call(body, 'stored')) {
      brewfatherUrl.set(body.stored ?? null);
    }
  } catch (e) {
    console.error('Error loading brewfather setting', e);
  }
}

// Save the brewfather override to the server (url may be null to clear)
export async function setBrewfatherUrl(url: string | null): Promise<void> {
  try {
    const res = await fetch('/api/settings/brewfather', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ url }),
    });
    if (!res.ok) {
      console.error('Failed to save brewfather setting', res.status);
      return;
    }
    const body = await res.json();
    brewfatherUrl.set(body.stored ?? null);
  } catch (e) {
    console.error('Error saving brewfather setting', e);
  }
}