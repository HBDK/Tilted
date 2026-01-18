<script lang="ts">
    import { sensorData } from '../lib/stores';
    
    // Helper function to get the latest data point
    $: latestData = $sensorData && $sensorData.dataPoints.length > 0 
      ? $sensorData.dataPoints[$sensorData.dataPoints.length - 1] 
      : null;

    // Next expected reading (ms since epoch) and relative status
    $: nextExpectedMs = latestData && typeof latestData.interval === 'number' && latestData.interval > 0
      ? latestData.timestamp + latestData.interval * 1000
      : null;

    // time delta in ms from now to nextExpectedMs (positive = in future, negative = overdue)
    $: timeUntilNextMs = nextExpectedMs ? nextExpectedMs - Date.now() : null;

    function formatRelative(ms: number | null): string {
      if (ms === null) return '';
      const abs = Math.abs(ms);
      if (abs < 60_000) {
        const s = Math.round(abs / 1000);
        return `${s}s ${ms >= 0 ? 'from now' : 'ago'}`;
      }
      if (abs < 60 * 60_000) {
        const m = Math.round(abs / 60000);
        return `${m}m ${ms >= 0 ? 'from now' : 'ago'}`;
      }
      const h = Math.round(abs / (60 * 60_000));
      return `${h}h ${ms >= 0 ? 'from now' : 'ago'}`;
    }

    // Compute averages for the selected period
    let avgTemp: number | null = null;
    let avgTilt: number | null = null;
  $: if ($sensorData && $sensorData.dataPoints.length > 0) {
      const points = $sensorData.dataPoints;
      let sumT = 0;
      let sumTilt = 0;
      let count = 0;
      for (let p of points) {
        // guard against missing values
        if (typeof p.temp === 'number' && !isNaN(p.temp)) {
          sumT += p.temp;
        }
        if (typeof p.tilt === 'number' && !isNaN(p.tilt)) {
          sumTilt += p.tilt;
        }
        count += 1;
      }
      if (count > 0) {
        avgTemp = sumT / count;
        avgTilt = sumTilt / count;
      }
    }
  </script>
  
  <div class="bg-white p-4 rounded-lg shadow-md">
    <h2 class="text-lg font-semibold mb-4">Current Sensor Readings</h2>
    
    {#if $sensorData && latestData}
      <div class="grid grid-cols-2 md:grid-cols-3 gap-4">
        <!-- Gravity Card -->
        <div class="p-3 bg-blue-50 rounded-lg border border-blue-100">
          <div class="text-sm text-blue-700 font-medium">Gravity</div>
          <div class="text-2xl font-bold">{latestData.gravity.toFixed(3)}</div>
        </div>
        
        <!-- Temperature Card -->
        <div class="p-3 bg-red-50 rounded-lg border border-red-100">
          <div class="text-sm text-red-700 font-medium">Temperature</div>
          <div class="text-2xl font-bold">{latestData.temp.toFixed(1)}°</div>
          {#if avgTemp !== null}
            <div class="text-xs text-red-600 mt-1">avg: {avgTemp.toFixed(1)}°</div>
          {/if}
        </div>
        
        <!-- Tilt Card -->
        <div class="p-3 bg-green-50 rounded-lg border border-green-100">
          <div class="text-sm text-green-700 font-medium">Tilt</div>
          <div class="text-2xl font-bold">{latestData.tilt.toFixed(1)}°</div>
          {#if avgTilt !== null}
            <div class="text-xs text-green-600 mt-1">avg: {avgTilt.toFixed(1)}°</div>
          {/if}
        </div>
        
        <!-- Voltage Card -->
        <div class="p-3 bg-yellow-50 rounded-lg border border-yellow-100">
          <div class="text-sm text-yellow-700 font-medium">Battery</div>
          <div class="text-2xl font-bold">{latestData.volt.toFixed(2)}V</div>
        </div>
        
        <!-- Interval Card -->
        <div class="p-3 bg-purple-50 rounded-lg border border-purple-100">
          <div class="text-sm text-purple-700 font-medium">Interval</div>
          <div class="text-2xl font-bold">{latestData.interval}s</div>
        </div>
        
        <!-- Last Update Card -->
        <div class="p-3 bg-gray-50 rounded-lg border border-gray-100">
          <div class="text-sm text-gray-700 font-medium">Last Update</div>
          <div class="text-sm font-medium">
            {new Date(latestData.timestamp).toLocaleString()}
          </div>
        </div>

        <!-- Next Expected Reading -->
        <div class="p-3 bg-indigo-50 rounded-lg border border-indigo-100">
          <div class="text-sm text-indigo-700 font-medium">Next Expected</div>
          {#if nextExpectedMs}
            <div class="text-sm font-medium">{new Date(nextExpectedMs).toLocaleString()}</div>
            <div class="text-xs text-indigo-600 mt-1">{timeUntilNextMs !== null ? formatRelative(timeUntilNextMs) : ''}</div>
            {#if timeUntilNextMs !== null && timeUntilNextMs < 0}
              <div class="text-xs text-red-600 mt-1">Overdue by {formatRelative(-timeUntilNextMs)}</div>
            {/if}
          {:else}
            <div class="text-sm text-gray-600">No interval available</div>
          {/if}
        </div>
      </div>
      
      <div class="mt-4 text-sm text-gray-500">
        <div>Sensor ID: <span class="font-medium">{$sensorData.sensorId}</span></div>
        <div>Gateway: <span class="font-medium">{$sensorData.gatewayName} ({$sensorData.gatewayId})</span></div>
      </div>
    {:else}
      <div class="text-gray-400 text-center py-4">
        No sensor data available
      </div>
    {/if}
  </div>