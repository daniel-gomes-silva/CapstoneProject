# Capstone Project

A [prototype solution](https://github.com/daniel-gomes-silva/RAPTOR) for public transport trip planning from the passenger's perspective has been implemented, based on the RAPTOR algorithm. However, some routes may consider connections (transfers) through walking paths that are currently calculated using simple metrics such as Manhattan distance. These metrics **do not consider the actual paths** (streets, avenues, etc.) of an urban network, so distances and **travel times may deviate significantly** from the real world.  
The **aim** is to improve the current implementation of the RAPTOR algorithm by **integrating an efficient algorithm and data structure that allows for (pre-)calculating the best walking paths between two points of the calculated routes**, which is the objective of this project. These points can be geographic coordinates (latitude, longitude) defined by the passenger, or previously defined points of interest (e.g., bus or metro stops).

## Table of Contents

- [Footpath Duration Calculation](#footpath-duration-calculation)
  - [Introduction](#introduction)
  - [How it Works (General)](#how-it-works-general)
  - [Prerequisites](#prerequisites)
  - [Sequential Version: footpathDuration.cpp](#sequential-version-footpathdurationcpp)
  - [Parallel Version: footpathDurationOMP.cpp](#parallel-version-footpathdurationompcpp)
  - [Output](#output)
- [Redis Integration for Caching](#redis-integration-for-caching)
  - [Prerequisites](#prerequisites-1)
  - [Caching Footpath Data: redis.cpp](#caching-footpath-data-rediscpp)
  - [Querying Cached Data: redisQueryExample.cpp](#querying-cached-data-redisqueryexamplecpp)
- [Footpath Distance Calculation: footpathDistance.cpp](#footpath-distance-calculation-footpathdistancecpp)
- [Usage with RAPTOR Algorithm](#usage-with-raptor-algorithm)
- [Open Source Routing Machine](#open-source-routing-machine)
  - [Using Docker](#using-docker)

## Footpath Duration Calculation

This project contains two C++ programs designed to calculate footpath durations between public transport stops (Metro and STCP) in Porto using the [OSRM](#open-source-routing-machine) (Open Source Routing Machine) API. One version is **sequential** `footpathDuration.cpp`, and the other `footpathDurationOMP.cpp` leverages OpenMP for **parallel** processing to improve performance.

### Introduction

These programs interact with a local **OSRM server to retrieve walking durations** between various public transport stops. The **goal** is to **create a comprehensive dataset of footpath travel times**, which can be valuable for transport planning and analysis. Additionally, the project includes tools to load this generated data into a **Redis cache**, enabling fast and efficient lookups for use in other applications.  
Currently, there are **85 Metro stops** and **2504 STCP stops**, resulting in a total of 2589 stops and 2589*(2588/2) = **3350166 unique pairs**. The sequential version processes requests one by one, while the parallel version uses OpenMP to send multiple requests concurrently, significantly reducing execution time for large datasets.

### How it Works (General)

1. **Data Loading**: Stop locations (latitude and longitude) are read from GTFS `stops.txt` files for both Metro and STCP services. All loaded stops are combined into a single list

2. **API Request Generation**: The program iterates through all possible unique pairs of stops (avoiding redundant calculations like A to B if B to A is already covered, and self-loops A to A). For each source stop, it creates batches of destination stops

3. **OSRM Interaction**:
    - For each batch, an HTTP GET request is sent to the local OSRM `table` service
    - The request URL is carefully constructed to specify the source and multiple destinations
    - `libcurl` handles the actual HTTP communication

4. **Response Parsing**: The JSON response from OSRM, containing the calculated durations, is parsed

5. **Data Storage**: The extracted durations and stop IDs are first written to a CSV file. This data can then be loaded into a Redis database using the provided `redis.cpp` program, creating a fast (and persistent) cache for quick queries by other services or applications

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
    - **OSRM URL Construction**: It constructs the OSRM [`table`](https://github.com/Project-OSRM/osrm-backend/blob/master/docs/http.md#table-service) service URL:  
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

The majority of the code is **identical to the sequential version**. The key differences are in the `main` function, specifically how API requests are managed and executed

- **`Request Struct`**: A new struct `Request` is introduced to encapsulate the parameters for each OSRM API call: `sourceIndex`, `destStart`, and `destEnd`. This makes it easier to manage individual tasks for parallel execution

- **`Request Batching`**: Instead of immediately making API calls in nested loops, this version first populates a `std::vector<Request> requests` with all the individual OSRM API requests that need to be made. This pre-computation allows OpenMP to effectively schedule these independent tasks

- **`#pragma omp parallel for schedule(dynamic)`**: This is the core OpenMP directive that enables parallel execution
    - `#pragma omp parallel for` tells the compiler to parallelize the for loop that follows. Each iteration of this loop (representing an OSRM API request) can potentially be run by a different thread
    - `schedule(dynamic)` this clause specifies how iterations are assigned to threads. `dynamic` scheduling means that chunks of iterations are assigned to threads as they become available. This is often good for loops where the workload for each iteration might vary (e.g., due to network latency in API calls), helping to balance the load among threads

- **`#pragma omp critical`**: The section of code where results are written to the `outFile` and shared counters (like `totalStopPairs`, `totalRequests`, `failedRequests`) are updated is enclosed within a `#pragma omp critical` block
    - This is crucial for thread safety. When multiple threads try to write to the same file or update shared variables simultaneously, it can lead to data corruption or race conditions
    - The `critical` directive ensures that only one thread can execute the code within that block at any given time, preventing conflicts

**How Parallelism Helps**

By using `omp parallel for`, multiple HTTP requests to the OSRM server can be made concurrently. While one thread is waiting for the OSRM server to respond to its request, other threads can be sending their own requests or processing their responses. This significantly reduces the total wall-clock time required to fetch all durations, especially when dealing with a large number of stops and network latency is a factor. For instance, the **sequential version** (`footpathDuration.cpp`) takes approximately **27 minutes** to complete, whereas the **parallel version** (`footpathDurationOMP.cpp`) finishes in less than **10 minutes** when calculating all **3350166 unique pairs** from the **2589 stops**.

### Output 

Both programs will generate a CSV file named `foot_durations.csv` (or `foot_durations_X.csv` if the file already exists) in the same directory as the executable. The file will have the following format:

```
stop_id,stop_id,duration
R52,PREL1,7299.2
R52,CAVE4,10908.7
R52,MCBL1,9583.3
R52,SBNT3,9639.5
R52,SCAL4,10409.9
...
```

Where:
- The first `stop_id` is the source stop
- The second `stop_id` is the destination stop
- `duration` is the walking time in seconds. A value of `-1` indicates that no route was found between the two stops by OSRM

Console output will include information about loaded stops, progress of API requests, and final statistics on total pairs processed, total API requests made, failed requests, and total execution time.

## Redis Integration for Caching

Once the footpath data is generated, it can be useful to store it in a fast, persistent cache for quick lookups by other applications or services. The following programs use [Redis](https://redis.io), an in-memory data store, for this purpose. This approach avoids the need to repeatedly read and parse the large CSV file

### Prerequisites

1. **Redis and hiredis** (required for `redis.cpp` and `redisQueryExample.cpp`):
    - **Redis Server**: An instance of Redis running at the default address `127.0.0.1:6379`
    - **hiredis**: The official C client library for Redis
        - On macOS (via Homebrew): `brew install redis hiredis`
2. **Generated CSV Data**: `foot_durations.csv` (or `foot_distances.csv`): 
    - This file must be generated first by running either the sequential (`footpathDuration.cpp`) or parallel (`footpathDurationOMP.cpp`) version described in the previous section. The CSV contains the walking durations/distances between all stop pairs that will be cached in Redis

### Caching Footpath Data: `redis.cpp`  
This program reads the generated CSV file `foot_durations.csv` (or `foot_distances.csv`) and loads its contents into a Redis database for fast and efficient access.

**Compilation && Execution**  
First, ensure you have a Redis server running
```bash
redis-server
```
Then, compile and run the program
```bash
g++ redis.cpp -lhiredis -o redis && ./redis
```

**Code Explanation**
- **Redis Connection**: It uses the `hiredis` library to connect to a local Redis server at the default address `127.0.0.1:6379`
- **Data Processing**:
    - Opens the specified CSV file (e.g., `foot_durations.csv`)
    - Reads the CSV file line by line. For each line, it parses the `stop_id1`, `stop_id2`, and the `duration` (or `distance`) value
- **`makeKey` Function**
    - A crucial helper function that creates a canonical key for each pair of stops. It ensures that the key for `(stopA, stopB)` is identical to the key for `(stopB, stopA)` by always placing the lexicographically smaller ID first
    - For example, `makeKey("BAR2", "5697")` and `makeKey("5697", "BAR2")` both produce the key `"5697:BAR2"`. This prevents duplicate entries and simplifies lookups
- **Redis `SET` Command**:
    - For each valid row in the CSV, it executes a Redis `SET` command
    - `redisCommand(redisContext, "SET %s %s", key.c_str(), duration.c_str())` stores the duration/distance as a value associated with the generated key

### Querying Cached Data: `redisQueryExample.cpp`  
This is a simple client program that demonstrates how to retrieve a single footpath duration/distance from the Redis cache using a key

**Compilation && Execution**  
```bash
g++ redisQueryExample.cpp -lhiredis -o redisQuery && ./redisQuery
```
```bash
Connected to Redis
Duration between BAR2 and 5697: 9905.7
```

**Code Explanation**

- **Connection and Key Generation**: Similar to `redis.cpp`, it connects to the local Redis server
    - It uses the **exact same** `makeKey` function to generate the key for the two hardcoded stop IDs (`stop_id1 = "BAR2"`, `stop_id2 = "5697"`). This is essential to ensure it can find the data stored by `redis.cpp`

- **Redis `GET` Command**: It uses the `redisCommand(redisContext, "GET %s", key.c_str())` function to send a `GET` request to Redis to fetch the value associated with the generated key

- **Handling the Reply**: The program checks the type of the `reply` from Redis
    - If the reply is of type `REDIS_REPLY_STRING`, it means the key was found, and the program prints the corresponding distance/duration
    - If the key does not exist or another error occurs, it prints a message indicating that no data was found for the given stops
- **Resource Management**: It properly frees the reply object (`freeReplyObject`) and the Redis connection context (`redisFree`) to prevent memory leaks

**Alternative: Command Line Query**  
You can also query the cached data directly using `redis-cli`
```bash
redis-cli GET "5697:BAR2"
```
```bash
"9905.7"
```

Check total number of cached entries
```bash
redis-cli DBSIZE
```
```bash
(integer) 3350166
```

Other useful commands:
```bash
# Search for keys containing a specific stop ID
redis-cli KEYS "*5697*"

# Check if Redis is running
redis-cli ping
```

## Footpath Distance Calculation: `footpathDistance.cpp`

This program is very similar to `footpathDuration.cpp` but calculates walking distances instead of durations

**Key Differences from `footpathDuration.cpp`**
- **API Request URL**: The primary difference is in the OSRM API request URL within the `getDistancesFromSource` function. It includes the `&annotations=distance` parameter to specifically request distances from the OSRM `table` service
    - `http://127.0.0.1:5001/table/v1/walking/<coordinates>?sources=0&destinations=<destinations_param>&annotations=distance`
- **Output File**: The output CSV file will be named `foot_distances.csv` and will contain distance instead of duration

## Usage with RAPTOR Algorithm

It's important to note that while `footpathDistance.cpp` provides distances, the output of `footpathDuration.cpp` (i.e., walking durations) is typically used by routing algorithms like [RAPTOR](https://github.com/daniel-gomes-silva/RAPTOR) (Rapid Transit Optimized Routing). RAPTOR primarily focuses on minimizing travel time, which includes walking durations between public transport stops and transfers.  
Although RAPTOR can alternatively use distance data from `footpathDistance.cpp` and apply a predefined constant average walking speed to estimate durations, the durations obtained directly from OSRM through `footpathDuration.cpp` should be more accurate.  
**My contributions to the RAPTOR algorithm implementation can be found [here](https://github.com/daniel-gomes-silva/RAPTOR).**

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