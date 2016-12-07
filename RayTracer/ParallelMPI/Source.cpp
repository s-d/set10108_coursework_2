#define _USE_MATH_DEFINES
#include <chrono>
#include <exception>
#include <fstream>
#include <iostream>
#include <math.h>
#include <mpi.h>
#include <set>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include <sstream>

using namespace std;
using namespace std::chrono;

// A simple random number generator.
double erand48(unsigned short seed[3]) { return (double)rand() / (double)RAND_MAX; }

// Vec structure to hold corrdinate or r, g, b color values
struct Vec {
  double x, y, z;

  // Vec constructor.
  Vec(double x_ = 0, double y_ = 0, double z_ = 0) {
    x = x_;
    y = y_;
    z = z_;
  }

  // Vec methods.
  Vec operator+(const Vec &b) const { return Vec(x + b.x, y + b.y, z + b.z); }
  Vec operator-(const Vec &b) const { return Vec(x - b.x, y - b.y, z - b.z); }
  Vec operator*(double b) const { return Vec(x * b, y * b, z * b); }
  Vec mult(const Vec &b) const { return Vec(x * b.x, y * b.y, z * b.z); }
  Vec &norm() { return *this = *this * (1 / sqrt(x * x + y * y + z * z)); }
  double dot(const Vec &b) const { return x * b.x + y * b.y + z * b.z; }
  Vec operator%(Vec &b) { return Vec(y * b.z - z * b.y, z * b.x - x * b.z, x * b.y - y * b.x); }
};

// A line with an origin and direction.
struct Ray {
  Vec origin, direction;
  Ray(Vec origin_, Vec direction_) : origin(origin_), direction(direction_) {}
};

// Sphere material types.
enum reflection_type { DIFFUSE, SPECULAR, REFRACTIVE };

// Sphere structure - takes a radius, position and colour.
struct Sphere {
  double radius;
  Vec position, emission, color;
  reflection_type reflection;

  // Sphere constructor.
  Sphere(double radius_, Vec position_, Vec emission_, Vec color_, reflection_type reflection_)
      : radius(radius_), position(position_), emission(emission_), color(color_), reflection(reflection_) {}

  // Returns distance of a ray intersection - 0 on a miss.
  double intersect(const Ray &ray) const {
    // Solve: t^2*d.d + 2*t*(o-p).d + (o-p).(o-p)-R^2 = 0.
    Vec op = position - ray.origin;
    double t;
    double eps = 1e-4;
    double b = op.dot(ray.direction);
    double det = b * b - op.dot(op) + radius * radius;

    if (det < 0) {
      return 0;
    } else {
      det = sqrt(det);
    }
    return (t = b - det) > eps ? t : ((t = b + det) > eps ? t : 0);
  }
};

// Scene to be rendered - made entierly of spheres.
Sphere spheres[] = {
    // Radius, position, emission, color, material.
    Sphere(1e5, Vec(1e5 + 1, 40.8, 81.6), Vec(), Vec(.75, .25, .25), DIFFUSE),   // Left
    Sphere(1e5, Vec(-1e5 + 99, 40.8, 81.6), Vec(), Vec(.25, .25, .75), DIFFUSE), // Rght
    Sphere(1e5, Vec(50, 40.8, 1e5), Vec(), Vec(.75, .75, .75), DIFFUSE),         // Back
    Sphere(1e5, Vec(50, 40.8, -1e5 + 170), Vec(), Vec(), DIFFUSE),               // Frnt
    Sphere(1e5, Vec(50, 1e5, 81.6), Vec(), Vec(.75, .75, .75), DIFFUSE),         // Botm
    Sphere(1e5, Vec(50, -1e5 + 81.6, 81.6), Vec(), Vec(.75, .75, .75), DIFFUSE), // Top
    Sphere(16.5, Vec(27, 16.5, 47), Vec(), Vec(1, 1, 1) * .999, SPECULAR),       // Mirr
    Sphere(16.5, Vec(73, 16.5, 78), Vec(), Vec(1, 1, 1) * .999, REFRACTIVE),     // Glas
    Sphere(600, Vec(50, 681.6 - .27, 81.6), Vec(12, 12, 12), Vec(), DIFFUSE)     // Lite
};

// Clamp unbounded colours to be within scale.
inline double clamp(double x) { return x < 0 ? 0 : x > 1 ? 1 : x; }

// Converts doubles to ints within pixel color scale (255).
inline int toInt(double x) { return int(pow(clamp(x), 1 / 2.2) * 255 + .5); }

// Intersect a ray with the scene - return true if it hits anything.
inline bool intersect(const Ray &ray, double &t, int &id) {
  double n = sizeof(spheres) / sizeof(Sphere);
  double inf = t = 1e20;
  double d;

  for (int i = int(n); i--;)
    if ((d = spheres[i].intersect(ray)) && d < t) {
      t = d;
      id = i;
    }

  return t < inf;
}

// Computes the radiance estimate along a ray.
Vec radiance(const Ray &r, int d, unsigned short *Xi) {
  Ray ray = r;
  int depth = d;
  double t;        // Distance to intersection
  int id = 0;      // ID of intersected object
  Vec cl(0, 0, 0); // Accumulated color
  Vec cf(1, 1, 1); // Accumulated reflectance

  while (1) {
    // If ray misses - return black.
    if (!intersect(ray, t, id)) {
      return cl;
    }

    const Sphere &obj = spheres[id];                                       // Object hit by ray.
    Vec x = ray.origin + ray.direction * t, n = (x - obj.position).norm(); // Ray intersection point.
    Vec nl = n.dot(ray.direction) < 0 ? n : n * -1;                        // Properly oriented surface normal.
    Vec f = obj.color;                                                     // Object color.
    double p = f.x > f.y && f.x > f.z ? f.x : f.y > f.z ? f.y : f.z;       // Max reflection color.
    cl = cl + cf.mult(obj.emission);

    // Russian roulette - 5 times.
    if (++depth > 5) {
      if (erand48(Xi) < p) {
        f = f * (1 / p);
      } else {
        return cl; // R.R.
      }
    }

    cf = cf.mult(f);

    // If object has a DIFFUSE reflection (not shiny)
    if (obj.reflection == DIFFUSE) {
      double r1 = 2 * M_PI * erand48(Xi);                                        // Angle
      double r2 = erand48(Xi), r2s = sqrt(r2);                                   // Distance from center.
      Vec w = nl;                                                                // Normal.
      Vec u = ((fabs(w.x) > .1 ? Vec(0, 1) : Vec(1)) % w).norm();                // Perpendicular to w.
      Vec v = w % u;                                                             // Perpendicular to u and w.
      Vec d = (u * cos(r1) * r2s + v * sin(r1) * r2s + w * sqrt(1 - r2)).norm(); // Random reflection ray.
      // return obj.e + f.mult(radiance(Ray(x,d),depth,Xi));
      ray = Ray(x, d); //
      continue;

      // If object has a SPECULAR reflection.
    } else if (obj.reflection == SPECULAR) {
      // return obj.e + f.mult(radiance(Ray(x,r.d-n*2*n.dot(r.d)),depth,Xi));
      ray = Ray(x, ray.direction - n * 2 * n.dot(ray.direction));
      continue;
    }

    Ray reflRay(x, ray.direction - n * 2 * n.dot(ray.direction)); // Ideal dielectric REFRACTION
    bool into = n.dot(nl) > 0;                                    // Ray from outside going in?
    double nc = 1, nt = 1.5;
    double nnt = into ? nc / nt : nt / nc;
    double ddn = ray.direction.dot(nl);
    double cos2t;

    if ((cos2t = 1 - nnt * nnt * (1 - ddn * ddn)) < 0) { // Total internal reflection
                                                         // return obj.e + f.mult(radiance(reflRay,depth,Xi));
      ray = reflRay;
      continue;
    }

    Vec tdir = (ray.direction * nnt - n * ((into ? 1 : -1) * (ddn * nnt + sqrt(cos2t)))).norm();
    double a = nt - nc, b = nt + nc, R0 = a * a / (b * b), c = 1 - (into ? -ddn : tdir.dot(n));
    double Re = R0 + (1 - R0) * c * c * c * c * c;
    double Tr = 1 - Re, P = .25 + .5 * Re;
    double RP = Re / P, TP = Tr / (1 - P);
    // return obj.e + f.mult(erand48(Xi)<P ?
    //		radiance(reflRay, depth,Xi) * RP:
    //		radiance(Ray(x, tdir), depth, Xi) * TP);
    if (erand48(Xi) < P) {
      cf = cf * RP;
      ray = reflRay;
    } else {
      cf = cf * TP;
      ray = Ray(x, tdir);
    }
    continue;
  }
}

MPI_Datatype createMPIVec() {

  MPI_Datatype VecType;
  MPI_Datatype type[3] = {MPI_DOUBLE, MPI_DOUBLE, MPI_DOUBLE};
  int blockLen[3] = {1, 1, 1};
  MPI_Aint disp[3];

  disp[0] = (MPI_Aint)offsetof(struct Vec, x);
  disp[1] = (MPI_Aint)offsetof(struct Vec, y);
  disp[2] = (MPI_Aint)offsetof(struct Vec, z);

  MPI_Type_create_struct(3, blockLen, disp, type, &VecType);
  MPI_Type_commit(&VecType);
  return VecType;
}

void execute(int width, int height, int samples, string time_stamp, int my_rank, int num_procs) {

  int w = width, h = height; // Image dimensions.
  int samps = samples;       // Number of samples.

  int chunk = h / num_procs;
  int chunk_end;

  chunk_end = (num_procs - (my_rank)) * chunk;

  Ray cam(Vec(50, 52, 295.6), Vec(0, -0.042612, -1).norm()); // Camera position and direction.
  Vec cx = Vec(w * .5135 / h);                               // X direction increment.
  Vec cy = (cx % cam.direction).norm() * .5135;              // Y direction increment.
  Vec r;                                                     // Colour samples.
  Vec *my_pixels = new Vec[w * chunk];                       // The image being rendered.
  MPI_Datatype mpi_vec = createMPIVec();

  for (int y = chunk * (num_procs - (my_rank + 1)); y < chunk_end; y++) { // Loop over image rows.

    unsigned short Xi[3] = {0, 0, y * y * y};

    for (unsigned short x = 0; x < w; x++) {
      for (int sy = 0, i = (chunk_end - y - 1) * w + x; sy < 2; sy++) {
        for (int sx = 0; sx < 2; sx++, r = Vec()) {
          for (int s = 0; s < samps; s++) {

            double r1 = 2 * erand48(Xi);
            double dx = r1 < 1 ? sqrt(r1) - 1 : 1 - sqrt(2 - r1);
            double r2 = 2 * erand48(Xi);
            double dy = r2 < 1 ? sqrt(r2) - 1 : 1 - sqrt(2 - r2);

            Vec d = cx * (((sx + .5 + dx) / 2 + x) / w - .5) + cy * (((sy + .5 + dy) / 2 + y) / h - .5) +
                    cam.direction; // Compute ray direction
            r = r + radiance(Ray(cam.origin + d * 140, d.norm()), 0, Xi) * (1. / samps);
          }
          // Camera rays are pushed ^^^^^ forward to start in interior.
          my_pixels[i] = my_pixels[i] + Vec(clamp(r.x), clamp(r.y), clamp(r.z)) * .25;
        }
      }
    }
  }

  vector<Vec> all_pixels; // Declare datastructure for all pixels

  if (my_rank == 0) {
    all_pixels.resize(w * h); // Initialize pixel data structure
    std::cout << "Commencing gather." << std::endl;
  } else {
  }
  // Gather individual processor pixels into proc 0.
  MPI_Gather(&my_pixels[0], chunk * w, mpi_vec, &all_pixels[0], chunk * w, mpi_vec, 0, MPI_COMM_WORLD);

  // Write pixel values to file.
  if (my_rank == 0) {
    std::cout << "Drawing image." << std::endl;
    FILE *f = fopen("image.ppm", "w");
    fprintf(f, "P3\n%d %d\n%d\n", w, h, 255);

    for (size_t i = 0; i < w * h; i++) {
      fprintf(f, "%d %d %d ", toInt(all_pixels[i].x), toInt(all_pixels[i].y), toInt(all_pixels[i].z));
    }
  }
}

int main(int argc, char *argv[]) {

	// Initialise MPI.
	int num_procs, my_rank;
	auto result = MPI_Init(&argc, &argv);

	if (result != MPI_SUCCESS) {
		MPI_Abort(MPI_COMM_WORLD, result);
		return -1;
	}

	// Get MPI info.
	MPI_Comm_size(MPI_COMM_WORLD, &num_procs);
	MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);

	std::ofstream data;
	time_point<system_clock> start_time;

	int width = 512;
	int hight = 512;

	char processor_name[MPI_MAX_PROCESSOR_NAME];
	int name_len;
	MPI_Get_processor_name(processor_name, &name_len);

	//vector<char*> names;

	/*string my_chars = "ab";

	vector<string> receive_data;
	if (my_rank == 0)
	{
		receive_data.resize(2);
		/*receive_data[0].resize(4);
		receive_data[1].resize(2);
	}

	cout << "echo chars" << endl;
	cout << &my_chars[0] << endl;

	char send[2];
	strcpy(send, my_chars.c_str());
	MPI
	MPI_Gather(&send[0], 2, MPI_CHAR, &receive_data[0], 2, MPI_CHAR, 0, MPI_COMM_WORLD);
	if (my_rank == 0)
	{
		  for (int i = 0; i < receive_data.size(); ++i) {
			  cout << "Im done" << receive_data[i] << "\n";
		  }
	}*/

	MPI_Info inf;
	char wut[2];
	itoa(my_rank, wut, 10);
	cout << "prekey: " << wut << " MyPro: " << processor_name << endl;
	MPI_Info_create(&inf);
	MPI_Info_set(inf, wut, processor_name);

	MPI_Barrier(MPI_COMM_WORLD);
	if (my_rank == 0) {
		char value[MPI_MAX_PROCESSOR_NAME];
		int flag;
		char tmp[2];
		for (int i = 0; i < num_procs; ++i) {
			itoa(i, tmp, 10);
			cout << "key: " << tmp << endl;
			MPI_Info_get(inf, tmp, MPI_MAX_PROCESSOR_NAME, value, &flag);
			cout << value << endl;
		}
	}


	MPI_Info_free(&inf);

  if (my_rank == 0) {

    // Get current time for timings file timestamp.
    auto time_stamp = to_string(duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
    // Create timings file.
    ofstream data("./Data/parallel_MPI_" + time_stamp + ".csv", ofstream::out);
    start_time = system_clock::now();
  }

  int samps = argc == 2 ? atoi(argv[1]) / 4 : 1;

  execute(512, 512, samps, "", my_rank, num_procs);

  if (my_rank == 0) {
    auto end_time = system_clock::now();
    auto total_time = duration_cast<milliseconds>(end_time - start_time).count();
    data << "," << total_time << std::endl;
    data.flush();
    data.close();
  }

  MPI_Finalize();
  return 0;
}