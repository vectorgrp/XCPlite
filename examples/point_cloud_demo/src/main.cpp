// point_cloud_demo - xcplib C++ example
// Simulates a 3D point cloud with simple physics to demonstrate visualization of 3-dimensional objects in CANapes 3D scene window
// All XCPlite related instrumentation code is marked with "XCP:" comments

#include <algorithm> // for std::min
#include <array>     // for std::array
#include <atomic>    // for std::atomic
#include <cmath>     // for sqrt, pow, cos, sin
#include <csignal>   // for signal handling
#include <cstdint>   // for uintxx_t
#include <ctime>     // for time
#include <iostream>  // for std::cout
#include <optional>  // for std::optional

#include <a2l.hpp>    // for xcplib A2l generation application programming interface
#include <xcplib.hpp> // for xcplib application programming interface

//-----------------------------------------------------------------------------------------------------
// XCP: Configuration

constexpr const char OPTION_PROJECT_NAME[] = "point_cloud_demo";
constexpr const char OPTION_PROJECT_VERSION[] = __TIME__;
constexpr bool OPTION_USE_TCP = true;
constexpr uint16_t OPTION_SERVER_PORT = 5555;
constexpr size_t OPTION_QUEUE_SIZE = 1024 * 64;
constexpr int OPTION_LOG_LEVEL = 4;
constexpr uint8_t OPTION_SERVER_ADDR[] = {0, 0, 0, 0};

//-----------------------------------------------------------------------------------------------------

namespace point_cloud {

// Calibration parameters of the point cloud simulation
struct ParametersT {
    uint32_t max_points;    // Maximum number of points in the cloud
    double boundary;        // boundary_ box size in m
    double min_radius;      // Minimum point radius in m
    double max_radius;      // Point radius in m
    double min_velocity;    // Minimum point velocity in m/s
    double max_velocity;    // Maximum point velocity in m/s
    double ttl_min;         // Minimum time to live in s
    double ttl_max;         // Maximum time to live in s
    double gravity;         // Gravity in m/s²
    uint32_t cycle_time_us; // Cycle time of a simulation step in microseconds
};

// Default parameter values
const ParametersT kParameters = {
    .max_points = 10,     // points in the cloud
    .boundary = 1.0,      // boundary_ in m
    .min_radius = 0.01,   // min_radius in m
    .max_radius = 0.05,   // max_radius in m
    .min_velocity = 0.1,  // min_velocity in m/s
    .max_velocity = 1.0,  // max_velocity in m/s
    .ttl_min = 3.0,       // ttl in seconds
    .ttl_max = 5.0,       // ttl in seconds
    .gravity = 0.0,       // gravity in m/s²
    .cycle_time_us = 1000 // cycle_time_us in microseconds
};

//-----------------------------------------------------------------------------------------------------

// Point struct representing a point in 3D space with velocity and radius
struct Point {
    float x;   // x coord in m
    float y;   // y coord in m
    float z;   // z coord in m
    float r;   // radius in m
    float ttl; // lifetime in s
    float v_x; // x velocity in m/s
    float v_y; // y velocity in m/s
    float v_z; // z velocity in m/s

    void clear() {
        x = 0.0f;
        y = 0.0f;
        z = 0.0f;
        ttl = 0.0f;
        v_x = 0.0f;
        v_y = 0.0f;
        v_z = 0.0f;
        r = 0.0f;
    }

    void move(double dx, double dy, double dz) {
        x += (float)dx;
        y += (float)dy;
        z += (float)dz;
    }

    void print() const {
        std::cout << "pos=(" << x << ", " << y << ", " << z << "), "
                  << "vel=(" << v_x << ", " << v_y << ", " << v_z << ")" << " r=" << r << std::endl;
    }
};

template <uint16_t N> class PointCloud {

  private:
    xcplib::CalSeg<ParametersT> params_; // XCP: Calibration parameter segment RAII wrapper for the ParametersT struct
    double boundary_;                    // Current boundary_ box size in m
    uint32_t step_counter_;              // Global step counter
    uint64_t simulation_time_;           // Last simulation step time a s
    uint64_t real_time_;                 // Current real time
    uint16_t count_;                     // Current number of points in the cloud
    std::array<Point, N> points_;        // Array of points

  private:
    // XCP: Register A2L typedefs
    void createA2L() {

        if (A2lOnce()) {

            // Register the calibration paramneter struct
            A2lTypedefBegin(ParametersT, &kParameters, "Typedef for ParametersT");
            A2lTypedefParameterComponent(max_points, "Maximum number of points in the cloud", "points", 1, N);
            A2lTypedefParameterComponent(boundary, "boundary_ box size in meters", "m", 0.1, 100.0);
            A2lTypedefParameterComponent(gravity, "Gravity in meters per second squared", "m/s²", 0.0, 1000.0);
            A2lTypedefParameterComponent(max_radius, "Maximum point radius in meters", "m", 0.01, 1.0);
            A2lTypedefParameterComponent(min_radius, "Minimum point radius in meters", "m", 0.01, 1.0);
            A2lTypedefParameterComponent(min_velocity, "Minimum point velocity in meters per second", "m/s", 0.001, 10.0);
            A2lTypedefParameterComponent(max_velocity, "Maximum point velocity in meters per second", "m/s", 0.001, 10.0);
            A2lTypedefParameterComponent(ttl_min, "Minimum time to live for points in seconds", "s", 0.1, 60.0);
            A2lTypedefParameterComponent(ttl_max, "Maximum time to live for points in seconds", "s", 0.1, 60.0);
            A2lTypedefParameterComponent(cycle_time_us, "Cycle time of a simulation step in microseconds", "us", 0, 1000000);
            A2lTypedefEnd();

            // Register the Point struct
            A2lTypedefBegin(Point, nullptr, "Typedef for Point");
            A2lTypedefMeasurementComponent(x, "X coordinate of the point");
            A2lTypedefMeasurementComponent(y, "Y coordinate of the point");
            A2lTypedefMeasurementComponent(z, "Z coordinate of the point");
            A2lTypedefMeasurementComponent(r, "Radius of the point");
            // A2lTypedefMeasurementComponent(v_x, "X velocity of the point");
            // A2lTypedefMeasurementComponent(v_y, "Y velocity of the point");
            // A2lTypedefMeasurementComponent(v_z, "Z velocity of the point");
            A2lTypedefEnd();

            // Not used
            // A2lTypedefBegin(PointCloud, this, "Typedef for PointCloud");
            // A2lTypedefMeasurementComponent(count_, "Current number of points in the cloud");
            // A2lTypedefMeasurementComponent(step_counter_, "Global step counter");
            // A2lTypedefComponent(points_, Point, N); // Array of N Points
            // A2lTypedefEnd();
        }
    }

    // Create a random number between min and max
    float rand_float(bool sign, float min, float max) {

        int r = rand();
        double res = min + ((double)r / RAND_MAX) * (max - min);
        if ((r & 1) && sign) {
            return (float)-res;
        } else {
            return (float)res;
        }
    }

    // Add a new point to the cloud
    // Create a point at (0,0,0) with random velocity (v_x, v_y, v_z) between -10.0 and +10.0 m/s
    void add_point() {
        auto params = params_.lock();
        if (count_ < params->max_points) {
            float min_vel = static_cast<float>(params->min_velocity);
            float max_vel = static_cast<float>(params->max_velocity);
            float min_rad = static_cast<float>(params->min_radius);
            float max_rad = static_cast<float>(params->max_radius);
            Point p = {
                .x = 0.0f,
                .y = 0.0f,
                .z = 0.0f,
                .r = rand_float(false, min_rad, max_rad),
                .ttl = rand_float(false, static_cast<float>(params->ttl_min), static_cast<float>(params->ttl_max)),
                .v_x = rand_float(true, min_vel, max_vel),
                .v_y = rand_float(true, min_vel, max_vel),
                .v_z = rand_float(true, min_vel, max_vel),
            };
            points_[count_] = p;
            if (step_counter_ < 1000)
                std::cout << step_counter_ << ": point " << count_ << " spawned (count=" << count_ << ")" << std::endl;
            count_++;
        }
    };

    // Remove a point from the cloud by index
    void remove_point(uint16_t index) {
        if (index < count_) {
            points_[index] = points_[count_ - 1];
            count_--;
            points_[count_].clear();
        }
    };

    // Remove points that are out of the boundary_
    void remove_out_boundary_points() {
        for (uint16_t i = 0; i < count_;) {
            if (std::abs(points_[i].x) > boundary_ || std::abs(points_[i].y) > boundary_ || std::abs(points_[i].z) > boundary_) {
                remove_point(i);
                if (step_counter_ < 1000)
                    std::cout << step_counter_ << ": point " << i << " removed (count=" << count_ << ")" << std::endl;
                continue; // skip i increment
            }
            i++;
        }
    }

    // Calculate Euclidean distance between two points
    double calc_distance(const Point &p1, const Point &p2) const { return sqrt(pow(p2.x - p1.x, 2) + pow(p2.y - p1.y, 2) + pow(p2.z - p1.z, 2)); }

    // Check for boundary_ collisions and respond
    void check_boundary_collisions() {
        for (uint16_t i = 0; i < count_; i++) {
            if (points_[i].x < -boundary_ + points_[i].r) {
                points_[i].x = (float)(-boundary_ + points_[i].r);
                points_[i].v_x = (float)fabs(points_[i].v_x);
            }
            if (points_[i].x > boundary_ - points_[i].r) {
                points_[i].x = (float)(boundary_ - points_[i].r);
                points_[i].v_x = (float)-fabs(points_[i].v_x);
            }
            if (points_[i].y < -boundary_ + points_[i].r) {
                points_[i].y = (float)(-boundary_ + points_[i].r);
                points_[i].v_y = (float)fabs(points_[i].v_y);
            }
            if (points_[i].y > boundary_ - points_[i].r) {
                points_[i].y = (float)(boundary_ - points_[i].r);
                points_[i].v_y = (float)-fabs(points_[i].v_y);
            }
            if (points_[i].z < -boundary_ + points_[i].r) {
                points_[i].z = (float)(-boundary_ + points_[i].r);
                points_[i].v_z = (float)fabs(points_[i].v_z);
            }
            if (points_[i].z > boundary_ - points_[i].r) {
                points_[i].z = (float)(boundary_ - points_[i].r);
                points_[i].v_z = (float)-fabs(points_[i].v_z);
            }
        }
    }

    // Check for collisions between points and respond
    void check_point_collisions() {
        for (int i = 0; i < count_;) {
            for (int j = i + 1; j < count_;) {
                double d = calc_distance(points_[i], points_[j]);
                if (d < points_[i].r + points_[j].r) {
                    if (d < points_[i].r + points_[j].r) {
                        // Merge points
                        if (points_[i].r >= points_[j].r) {
                            // i absorbs j
                            points_[i].r += points_[j].r * 0.1f; // increase radius
                            points_[i].v_x = (points_[i].v_x + points_[j].v_x) * 0.5f;
                            points_[i].v_y = (points_[i].v_y + points_[j].v_y) * 0.5f;
                            points_[i].v_z = (points_[i].v_z + points_[j].v_z) * 0.5f;
                            remove_point(j);
                            break; // j was removed, exit inner loop

                        } else {
                            // j absorbs i
                            points_[j].r += points_[i].r * 0.1f; // increase radius
                            points_[j].v_x = (points_[i].v_x + points_[j].v_x) * 0.5f;
                            points_[j].v_y = (points_[i].v_y + points_[j].v_y) * 0.5f;
                            points_[j].v_z = (points_[i].v_z + points_[j].v_z) * 0.5f;
                            remove_point(i);
                            break; // i was removed, exit inner loop
                        }
                    } else {
                        // Simple elastic collision response
                        std::swap(points_[i].v_x, points_[j].v_x);
                        std::swap(points_[i].v_y, points_[j].v_y);
                        std::swap(points_[i].v_z, points_[j].v_z);
                    }
                }
                j++;
            }
            i++;
        }
    }

    // Check and remove points that have exceeded their lifetime (shrinking to zero radius)
    void check_lifetime() {
        auto params = params_.lock();

        float delta_t = (float)(params->cycle_time_us / 1e6f); // s
        for (uint16_t i = 0; i < count_;) {
            points_[i].ttl -= (float)delta_t;
            if (points_[i].ttl <= 0.0f) {
                remove_point(i);

                continue; // skip i increment
            }
            i++;
        }
    }

    // Decrease point radius over time in each time step to visualize lifetime
    void update_radius() {
        auto params = params_.lock();

        float delta_t = (float)(params->cycle_time_us / 1e6f); // s
        for (uint16_t i = 0; i < count_; i++) {
            float life_ratio = points_[i].ttl / (points_[i].ttl + delta_t);
            points_[i].r *= life_ratio;
        }
    }

  public:
    // Constructor
    PointCloud() : step_counter_(0), count_(0), boundary_(kParameters.boundary), params_("Parameters", &kParameters) {

        for (uint16_t i = 0; i < N; i++) {
            points_[i].clear();
        }

        // XCP: A2L type registrations
        createA2L();

        // XCP: Create the A2L instance for the calibration parameters
        params_.CreateA2lTypedefInstance("ParametersT", "Point cloud simulation parameters");

        // Create a cyclic event for the simulation step measurement
        // Specify cycle_time_us in microseconds to enable time downscaling in CANape using the cyclic mode
        DaqCreateCyclicEvent(step, kParameters.cycle_time_us);

        std::cout << "PointCloud<" << (unsigned int)N << "> instance created" << std::endl;

        real_time_ = simulation_time_ = ApplXcpGetClock64();
    }

    // Destructor
    ~PointCloud() = default;

    // Getters
    uint16_t get_count() const { return count_; }
    uint32_t get_step_counter() const { return step_counter_; }
    const Point &get_point(uint16_t index) const { return points_[index]; }

    // Perform a simulation step: move points, check for collisions
    bool step() {

        real_time_ = ApplXcpGetClock64();

        // Cycle timer
        uint64_t delta_time = params_.lock()->cycle_time_us * 1000; // in ns, assumes xcplib is compiled with OPTION_CLOCK_TICKS_1NS
        if (real_time_ - simulation_time_ < delta_time) {
            return false; // not time yet
        }

        simulation_time_ += delta_time;
        step_counter_++;

        // Check parameter changes of boundary_ and remove out-of-boundary points
        auto params = params_.lock();
        if (boundary_ != params->boundary) {
            std::cout << "boundary_ changed from " << boundary_ << " to " << params->boundary << std::endl;
            remove_out_boundary_points();
        }
        boundary_ = params->boundary;

        // Add a new point to the point if there is capacity and free space at the center
        if (count_ < params->max_points) {
            bool space_free = true;
            for (uint16_t i = 0; i < count_; i++) {
                double dist = calc_distance(points_[i], Point{.x = 0.0f, .y = 0.0f, .z = 0.0f, .r = 0.0f, .ttl = 0.0f, .v_x = 0.0f, .v_y = 0.0f, .v_z = 0.0f});
                if (dist < points_[i].r + static_cast<float>(params->max_radius)) {
                    space_free = false;
                    break;
                }
            }
            if (space_free) {
                add_point();
            }
        }

        // Move
        double delta_t = (double)params_.lock()->cycle_time_us / 1e6; // s
        for (uint16_t i = 0; i < count_; i++) {
            points_[i].move((double)points_[i].v_x * delta_t, (double)points_[i].v_y * delta_t, (double)points_[i].v_z * delta_t);
        }

        // Apply gravity to z velocity
        for (uint16_t i = 0; i < count_; i++) {
            points_[i].v_z -= static_cast<float>(params->gravity * delta_t);
        }

        // Check collisions
        check_boundary_collisions();
        check_point_collisions();

        // Lifetime
        check_lifetime();
        update_radius();

        // XCP: Model step measurement event
        DaqEventAtVar(step, simulation_time_,                                      //
                      A2L_MEAS(count_, "Current point count"),                     //
                      A2L_MEAS(real_time_, "Current real time in ns"),             //
                      A2L_MEAS(boundary_, "Current boundary box size in meters"),  //
                      A2L_MEAS(step_counter_, "Step counter"),                     //
                      A2L_MEAS_INST_ARRAY(points_, "Point", "Points in the cloud") // Array of Point instances

        );

        return true;
    }
};

} // namespace point_cloud

//-----------------------------------------------------------------------------------------------------

// Signal handler for graceful exit on Ctrl+C
std::atomic<bool> gRun{true};
void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        gRun = false;
    }
}

int main() {

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::cout << "\nXCPlite point cloud demo\n" << std::endl;

    // Initialize random seed
    srand(static_cast<unsigned int>(time(nullptr)));

    // XCP: Initialize
    XcpSetLogLevel(OPTION_LOG_LEVEL);                                                                   // Set log level (1-error, 2-warning, 3-info, 4-show XCP commands)
    XcpInit(OPTION_PROJECT_NAME, OPTION_PROJECT_VERSION /* EPK version*/, true /* activate */);         // Initialize the XCP singleton and activate XCP
    if (!XcpEthServerInit(OPTION_SERVER_ADDR, OPTION_SERVER_PORT, OPTION_USE_TCP, OPTION_QUEUE_SIZE)) { // Initialize the XCP Server
        std::cerr << "Failed to initialize XCP server" << std::endl;
        return 1;
    }
    if (!A2lInit(OPTION_SERVER_ADDR, OPTION_SERVER_PORT, OPTION_USE_TCP,
                 A2L_MODE_WRITE_ONCE | A2L_MODE_FINALIZE_ON_CONNECT | A2L_MODE_AUTO_GROUPS)) { // Enable runtime A2L generation for data declaration as code
        std::cerr << "Failed to initialize A2L generator" << std::endl;
        return 1;
    }

    // Create a PointCloud instance with max N points
    point_cloud::PointCloud<1000> point_cloud;

    // Main loop
    std::cout << "\nStarting main loop... (Press Ctrl+C to exit)" << std::endl;
    while (gRun) {

        // Calculate a simulation step
        if (!point_cloud.step()) {
            sleepUs(10); // Yield, if nothing to do
        }

        if (point_cloud.get_step_counter() == 1000) {
            A2lFinalize(); // Finalize A2L generation to make it available for inspection without a tool connected
        }
    }

    std::cout << "\nExiting ..." << std::endl;

    // XCP: Shutdown
    XcpDisconnect();
    XcpEthServerShutdown();

    return 0;
}
