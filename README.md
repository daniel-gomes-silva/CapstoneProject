## Footpath Duration Calculation

This project contains two C++ programs designed to calculate footpath durations between public transport stops (Metro and STCP) in Porto using the OSRM (Open Source Routing Machine) API. One version is **sequential** `footpathDuration.cpp`, and the other `footpathDurationOMP.cpp` leverages OpenMP for **parallel** processing to improve performance. Additionally, there is a `footpathDistance.cpp` program for calculating footpath distances.

### Introduction

These programs interact with a local OSRM server to retrieve walking durations between various public transport stops. The goal is to create a comprehensive dataset of footpath travel times, which can be valuable for transport planning and analysis. Currently, there are `85 Metro stops` and `2504 STCP stops`, resulting in a total of 2589 stops and 2589*(2588/2) = `3350166 unique pairs`.
The sequential version processes requests one by one, while the parallel version uses OpenMP to send multiple requests concurrently, significantly reducing execution time for large datasets.

### How it Works (General)

1. **Data Loading**: Stop locations (latitude and longitude) are read from GTFS `stops.txt` files for both Metro and STCP services. All loaded stops are combined into a single list

2. **API Request Generation**: The program iterates through all possible unique pairs of stops (avoiding redundant calculations like A to B if B to A is already covered, and self-loops A to A). For each source stop, it creates batches of destination stops

3. **OSRM Interaction**:
    - For each batch, an HTTP GET request is sent to the local OSRM `table` service
    - The request URL is carefully constructed to specify the source and multiple destinations
    - `libcurl` handles the actual HTTP communication

4. **Response Parsing**: The JSON response from OSRM, containing the calculated durations, is parsed

5. **Data Storage**: The extracted durations, along with the corresponding stop IDs, are written to a CSV file

6. **Parallelism (OpenMP version)**: The process of sending and receiving API requests is parallelized using OpenMP directives, allowing multiple requests to be handled concurrently by different threads, significantly speeding up the overall process

### Prerequisites

1. **OSRM Backend**: A running OSRM server for Portugal. You can set this up by following the [Open Source Routing Machine](#open-source-routing-machine) section below.
The programs expect the OSRM server to be running locally at `http://127.0.0.1:5001`

2. **libcurl**: A library for making HTTP requests
    - On macOS (via Homebrew): `brew install curl`

3. **json.hpp**: A single-header JSON library
    - You'll need to place `json.hpp` in your include path or the same directory as the source files

4. **GTFS files** (`stops.txt`):
    - `./datasets/Porto/metro/GTFS/stops.txt`
    - `./datasets/Porto/stcp/GTFS/stops.txt`  
    These files should contain stop information in GTFS format, specifically with `stop_id`, `stop_lat`, and `stop_lon`

### Sequential Version: `footpathDuration.cpp`

**Compilation && Execution**
```bash
g++ footpathDuration.cpp -lcurl -o footpathDuration && ./footpathDuration
```

**Code Explanation**

- `Stop Struct`: Defines a simple structure to hold `stop_id`, `latitude`, and `longitude` for each transport stop
- `loadMetroStops(const std::string &filename)` and `loadStcpStops(const std::string &filename)`:
    - Load stop data from CSV files, parsing `stop_id`, `stop_lat`, and `stop_lon` fields
    - Returns a `std::vector<Stop>` with all loaded stops
- `getDurationsFromSource(const std::vector<Stop> &stops, int sourceIndex, int destStart, int destEnd)`:
    - **Core function** for making OSRM API calls that takes a vector of (all) `Stop` objects, the `sourceIndex` of the origin stop, and a range of destination indices (`destStart` to `destEnd`)
    - **Coordinates String Construction**: It builds a string of coordinates in `longitude,latitude;longitude,latitude...` format, starting with the source and then all specified destinations
    - **Destinations String Construction**: Generates a string of destinations in the format required by OSRM (e.g., "1;2;3" for three destinations) calling `createDestinationsParam(int numCoordinates)`
    - **OSRM URL Construction**: It constructs the OSRM `table` service URL:  
    `http://127.0.0.1:5001/table/v1/walking/<coordinates>?sources=0&destinations=<destinations_param>`
        - `sources=0` indicates that the first coordinate in the coordinates string is the source
        - `destinations` specifies which of the subsequent coordinates are targets
    - **libcurl Usage**: Uses libcurl to `initialize`, `configure`, `perform HTTP requests` to OSRM, and `cleanup` resources
    - **JSON Parsing**: Parses the JSON response (using nlohmann::json) and extracts walking durations, returning a vector of durations in seconds (-1.0 for unreachable destinations)

- **Main Loop (Sequential Processing)** - `main()`:
    - Iterates through `allStops` to use each stop as a `sourceIndex`
    - For each source, it iterates through subsequent stops as `destinations` in batches of `MAX_DESTINATIONS` (set to 99, as OSRM has a limit of 100 locations per request, with one being the source)
    - Calls `getDurationsFromSource` to get durations for the current batch
    - Writes the `source_id,destination_id,duration` to the output (e.g., `foot_durations.csv`) CSV file. If a route is not found, `-1` is written



### Parallel Version: `footpathDurationOMP.cpp`

**Compilation && Execution**

To compile `footpathDurationOMP.cpp` with OpenMP support, you'll need to include OpenMP flags. The exact flags may vary slightly depending on your compiler and OS  

`On macOS` (via Homebrew): 

```bash
brew install llvm libomp
```

- **Intel Mac:**

```bash
clang++ footpathDurationOMP.cpp -lcurl -Xpreprocessor -fopenmp -lomp -L/usr/local/opt/libomp/lib -I/usr/local/opt/libomp/include -o footpathDurationOMP && ./footpathDurationOMP
```

- **Apple Silicon Mac:**

```bash
clang++ footpathDurationOMP.cpp -lcurl -Xpreprocessor -fopenmp -lomp -L/opt/homebrew/opt/libomp/lib -I/opt/homebrew/opt/libomp/include -o footpathDurationOMP && ./footpathDurationOMP
```

**Code Explanation**

The majority of the code is `identical to the sequential version`. The key differences are in the `main` function, specifically how API requests are managed and executed.

- `Request Struct`: A new struct `Request` is introduced to encapsulate the parameters for each OSRM API call: `sourceIndex`, `destStart`, and `destEnd`. This makes it easier to manage individual tasks for parallel execution

- `Request Batching`: Instead of immediately making API calls in nested loops, this version first populates a `std::vector<Request> requests` with all the individual OSRM API requests that need to be made. This pre-computation allows OpenMP to effectively schedule these independent tasks

- `#pragma omp parallel for schedule(dynamic)`: This is the core OpenMP directive that enables parallel execution
    - `#pragma omp parallel for` tells the compiler to parallelize the for loop that follows. Each iteration of this loop (representing an OSRM API request) can potentially be run by a different thread
    - `schedule(dynamic)` this clause specifies how iterations are assigned to threads. `dynamic` scheduling means that chunks of iterations are assigned to threads as they become available. This is often good for loops where the workload for each iteration might vary (e.g., due to network latency in API calls), helping to balance the load among threads

- `#pragma omp critical`: The section of code where results are written to the `outFile` and shared counters (like `totalStopPairs`, `totalRequests`, `failedRequests`) are updated is enclosed within a `#pragma omp critical` block
    - This is crucial for thread safety. When multiple threads try to write to the same file or update shared variables simultaneously, it can lead to data corruption or race conditions
    - The `critical` directive ensures that only one thread can execute the code within that block at any given time, preventing conflicts


**How Parallelism Helps**

By using `omp parallel for`, multiple HTTP requests to the OSRM server can be made concurrently. While one thread is waiting for the OSRM server to respond to its request, other threads can be sending their own requests or processing their responses. This significantly reduces the total wall-clock time required to fetch all durations, especially when dealing with a large number of stops and network latency is a factor.  
For instance, the **sequential version** (`footpathDuration.cpp`) takes approximately **27 minutes** to complete, whereas the **parallel version** (`footpathDurationOMP.cpp`) finishes in about **10 minutes** when calculating all **3350166 unique pairs** from the **2589 stops**.

### Output 

Both programs will generate a CSV file named `foot_durations.csv` (or `foot_durations_X.csv` if the file already exists) in the same directory as the executable. The file will have the following format:

```
stop_id,stop_id,duration
R52,PREL1,7299.2
R52,CAVE4,10908.7
R52,MCBL1,9583.3
R52,SBNT3,9639.5
R52,SCAL4,10409.9
```

Where:
- The first `stop_id` is the source stop
- The second `stop_id` is the destination stop
- `duration` is the walking time in seconds. A value of `-1` indicates that no route was found between the two stops by OSRM

Console output will include information about loaded stops, progress of API requests, and final statistics on total pairs processed, total API requests made, failed requests, and total execution time.

### Footpath Distance Calculation: `footpathDistance.cpp`

This program is very similar to footpathDuration.cpp but calculates walking distances instead of durations.

**Key Differences from `footpathDuration.cpp`**
- **API Request URL**: The primary difference is in the OSRM API request URL within the `getDistancesFromSource` function. It includes the `&annotations=distance` parameter to specifically request distances from the OSRM `table` service
    - `http://127.0.0.1:5001/table/v1/walking/<coordinates>?sources=0&destinations=<destinations_param>&annotations=distance`
- **Output File**: The output CSV file will be named `foot_distances.csv` and will contain distance instead of duration

### Usage with RAPTOR Algorithm

It's important to note that while `footpathDistance.cpp` provides distances, the output of `footpathDuration.cpp` (i.e., walking durations) is typically used by routing algorithms like [RAPTOR](https://github.com/daniel-gomes-silva/RAPTOR) (Rapid Transit Optimized Routing). RAPTOR primarily focuses on minimizing travel time, which includes walking durations between public transport stops and transfers.

<br>

## Open Source Routing Machine

High performance routing engine written in C++ designed to run on OpenStreetMap data.

This project uses [OSRM](https://project-osrm.org) running in `Docker` and accessed via `HTTP API` to use the following [services](https://github.com/Project-OSRM/osrm-backend/blob/master/docs/http.md):
- **Route** - Finds the fastest route between coordinates
- **Table** - Computes the duration or distances of the fastest route between all pairs of supplied coordinates

To quickly try OSRM, you can use their [demo server](https://map.project-osrm.org) which comes with both the backend and a frontend on top.

### Using Docker

Download OpenStreetMap extracts for example from [Geofabrik](https://download.geofabrik.de)

```bash
wget http://download.geofabrik.de/europe/portugal-latest.osm.pbf
```

Pre-process the extract with the `foot profile` and start a routing engine HTTP server on port 5000

```bash
docker run -t -v "${PWD}:/data" ghcr.io/project-osrm/osrm-backend osrm-extract -p /opt/foot.lua /data/portugal-latest.osm.pbf || echo "osrm-extract failed"
```

The flag `-v "${PWD}:/data"` creates the directory `/data` inside the docker container and makes the current working directory `"${PWD}"` available there. The file `/data/portugal-latest.osm.pbf` inside the container is referring to `"${PWD}/portugal-latest.osm.pbf"` on the host. Noting that this process can take a `long time to complete` with little changes on the terminal output, for example, a Portugal OSM file of 359MB took around 8 minutes to finish extraction and generate edge-expanded graph representation. You may need to `increase Docker resource allocation`.

```bash
docker run -t -v "${PWD}:/data" ghcr.io/project-osrm/osrm-backend osrm-partition /data/portugal-latest.osrm || echo "osrm-partition failed"
docker run -t -v "${PWD}:/data" ghcr.io/project-osrm/osrm-backend osrm-customize /data/portugal-latest.osrm || echo "osrm-customize failed"
```

Note there is no `portugal-latest.osrm` file, but multiple `portugal-latest.osrm.*` files, i.e. `portugal-latest.osrm` is not file path, but "base" path referring to set of files and there is an option to omit this `.osrm` suffix completely(e.g. `osrm-partition /data/portugal-latest`).

```bash
docker run -t -i -p 5000:5000 -v "${PWD}:/data" ghcr.io/project-osrm/osrm-backend osrm-routed --algorithm mld /data/portugal-latest.osrm
```
If port 5000 is already in use, you can use a different port (e.g., 5001)

```bash
docker run -t -i -p 5001:5000 -v "${PWD}:/data" ghcr.io/project-osrm/osrm-backend osrm-routed --algorithm mld /data/portugal-latest.osrm
```

Make requests against the HTTP server

```bash
curl "http://127.0.0.1:5000/route/v1/walking/-8.598445,41.177760;-8.599072,41.167070?steps=true"
```

This request finds the fastest `route` between coordinates in the supplied order. In this example, it calculates the `walking` route `from FEUP` (-8.598445,41.177760) `to OPT` (-8.599072,41.167070), and the `steps=true` parameter includes turn-by-turn directions in the response.  
OSRM coordinate order is longitude,latitude - not latitude,longitude like Google Maps.