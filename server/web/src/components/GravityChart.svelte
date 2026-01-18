<script lang="ts">
  import { onMount } from "svelte";
  import { sensorData } from "../lib/stores";
  import type { DataPoint } from "../lib/types";

  let chartElement: HTMLCanvasElement;
  let chart: any = null;
  let chartLibrary: any = null;

  $: if ($sensorData && chartElement && chartLibrary) {
    renderChart($sensorData.dataPoints);
  }

  onMount(async () => {
    const ChartModule = await import("chart.js");
    const {
      Chart,
      CategoryScale,
      LinearScale,
      PointElement,
      LineElement,
      Title,
      Tooltip,
      Legend,
      LineController,
      Colors,
    } = ChartModule;

    // Register required components
    Chart.register(
      CategoryScale,
      LinearScale,
      PointElement,
      LineElement,
      LineController, // Important: Register the Line chart controller
      Title,
      Tooltip,
      Legend,
      Colors,
    );

    // Store reference to the Chart library for later use
    chartLibrary = { Chart };

    // Initial render if data is available
    if ($sensorData && $sensorData.dataPoints.length > 0) {
      renderChart($sensorData.dataPoints);
    }
  });

  function formatTimestamp(timestamp: number): string {
    return new Date(timestamp).toLocaleTimeString([], {
      hour: "2-digit",
      minute: "2-digit",
      month: "short",
      day: "numeric",
    });
  }

  function renderChart(dataPoints: DataPoint[]): void {
    if (!dataPoints || dataPoints.length === 0 || !chartLibrary) return;

    // Destroy existing chart if it exists
    if (chart) {
      chart.destroy();
    }

    const ctx = chartElement.getContext("2d");
    if (!ctx) return;

  // Use numeric x values so spacing is proportional to timestamps
  const gravityData = dataPoints.map((dp) => ({ x: dp.timestamp, y: dp.gravity }));
  const tempData = dataPoints.map((dp) => ({ x: dp.timestamp, y: dp.temp }));

    chart = new chartLibrary.Chart(ctx, {
      type: "line",
      data: {
        datasets: [
          {
            label: "Gravity",
            data: gravityData,
            tension: 0.2,
            pointRadius: 3,
            yAxisID: "y",
            borderColor: '#1f77b4',
            backgroundColor: '#1f77b4'
          },
          {
            label: "Temperature",
            data: tempData,
            tension: 0.2,
            pointRadius: 3,
            yAxisID: "y1",
            borderColor: '#ff7f0e',
            backgroundColor: '#ff7f0e'
          },
        ],
      },
      options: {
        responsive: true,
        maintainAspectRatio: false,
        plugins: {
          legend: {
            position: "top",
          },
          title: {
            display: true,
            text: "Gravity and Temperature Readings Over Time",
          },
          tooltip: {
            callbacks: {
              title: function (context: any) {
                // Use the x value (timestamp) from the first item
                const item = context[0];
                const ts = item && item.parsed && item.parsed.x ? item.parsed.x : null;
                return ts ? new Date(Number(ts)).toLocaleString() : '';
              },
              afterLabel: function (context: any) {
                const dataIndex = context.dataIndex;
                const dataPoint = dataPoints[dataIndex];

                // Only add the battery and tilt info once (not for each dataset)
                if (context.datasetIndex === 0 && dataPoint) {
                  return [
                    `Battery: ${dataPoint.volt.toFixed(2)}V`,
                    `Tilt: ${dataPoint.tilt.toFixed(1)}°`,
                  ];
                }
                return null;
              },
            },
          },
        },
        scales: {
          y: {
            type: "linear",
            display: true,
            position: "left",
            beginAtZero: false,
            title: {
              display: true,
              text: "Gravity",
            },
            grid: {
              drawOnChartArea: true,
            },
            ticks: {
              callback: function (value: any) {
                return value.toFixed(3);
              },
            },
          },
          y1: {
            type: "linear",
            display: true,
            position: "right",
            beginAtZero: false,
            title: {
              display: true,
              text: "Temperature (°C)",
            },
            grid: {
              drawOnChartArea: false,
            },
          },
          x: {
            type: 'linear',
            title: {
              display: true,
              text: 'Time'
            },
            ticks: {
              maxRotation: 45,
              minRotation: 45,
              callback: function(value: any) {
                // value is the numeric timestamp
                if (!value) return '';
                return new Date(Number(value)).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
              }
            }
          },
        },
      },
    });
  }
</script>

<div class="bg-white p-4 rounded-lg shadow-md">
  <div class="w-full h-64">
    {#if $sensorData && $sensorData.dataPoints.length > 0}
      <canvas bind:this={chartElement}></canvas>
    {:else}
      <div class="w-full h-full flex items-center justify-center text-gray-400">
        No data available for this sensor
      </div>
    {/if}
  </div>
</div>
