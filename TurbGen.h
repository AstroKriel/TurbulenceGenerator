// *******************************************************************************
// *************************** Turbulence generator ******************************
// *******************************************************************************
//
// This header file contains functions and data structures used by the turbulence
// generator. The main input (parameter) file is 'turbulence_generator.inp'.
//
// Please see Federrath et al. (2010, A&A 512, A81) for details and cite :)
//
// DESCRIPTION
//
//  Contains functions to compute the time-dependent physical turbulent vector
//  field used to drive or initialise turbulence in hydro codes such as AREPO,
//  FLASH, GADGET, PHANTOM, PLUTO, QUOKKA, etc.
//  The driving sequence follows an Ornstein-Uhlenbeck (OU) process.
//
//  For example applications see Federrath et al. (2008, ApJ 688, L79);
//  Federrath et al. (2010, A&A 512, A81); Federrath (2013, MNRAS 436, 1245);
//  Federrath et al. (2021, Nature Astronomy 5, 365)
//
// AUTHOR: Christoph Federrath, 2008-2022
//
// *******************************************************************************

#ifndef TURBULENCE_GENERATOR_H
#define TURBULENCE_GENERATOR_H

#include <iostream>
#include <string>
#include <vector>
#include <algorithm>

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <float.h>
#include <string.h>
#include <math.h>

namespace NameSpaceTurbGen {
    // constants
    static const int tgd_max_n_modes = 100000;
}

/*********************************************************************************
 *
 * TurbGen class
 *   Generates turbulent vector fields for setting turbulent initial conditions,
 *   or continuous driving of turbulence based on an Ornstein-Uhlenbeck process.
 *
 * @author Christoph Federrath (christoph.federrath@anu.edu.au)
 * @version 2022
 *
 *********************************************************************************/

class TurbGen
{
    private:
        enum {X, Y, Z};
        bool verbose; // extra output for debugging
        std::string ClassSignature; // class signature
        std::string parameter_file; // parameter file for controlling turbulence driving
        std::vector<double> mode[3], aka[3], akb[3], OUphases, ampl; // modes arrays, phases, amplitudes
        int PE; // MPI task for printf purposes, if provided
        int n_modes; // number of modes
        int ndim; // number of spatial dimensions
        int random_seed, seed; // 'random seed' is the original starting seed, then 'seed' gets updated by call to random number generator
        int spect_form; // spectral form (Band, Parabola, Power Law)
        int nsteps_per_turnover_time; // number of driving patterns per turnover time
        int step; // internal OU step number
        double L[3]; // domain physical length L[dim] with dim = X, Y, Z
        double velocity; // velocity dispersion
        double t_decay; // turbulent turnover time (auto-correlation timescale)
        double dt; // time step for OU update and for generating driving patterns
        double energy; // driving energy injection rate (~ v^3 / L)
        double OUvar; // OU variance corresponding to turbulent turnover (decay) time and energy input rate (OUvar = sqrt(energy/t_decay))
        double stir_min, stir_max; // min and max wavnumber for driving or single realisation
        double sol_weight, sol_weight_norm; // weight for decomposition into solenoidal and compressive modes
        double power_law_exp; // for power-law spectrum (spect_form = 2): exponent
        double angles_exp; // for power-law spectrum (spect_form = 2): angles exponent for sparse sampling

    /// Constructors
    public: TurbGen(void)
    {
        Constructor(0, false);
    };
    public: TurbGen(const int PE, const bool verbose)
    {
        Constructor(PE, verbose);
    };
    public: TurbGen(const int PE)
    {
        Constructor(PE, false);
    };
    public: TurbGen(const bool verbose)
    {
        Constructor(0, verbose);
    };
    /// Destructor
    public: ~TurbGen()
    {
      if (verbose) std::cout<<ClassSignature<<"destructor called."<<std::endl;
    };
    // General constructur
    private: void Constructor(const int PE, const bool verbose)
    {
        ClassSignature = "TurbGen: ";
        this->verbose = verbose;
        this->PE = PE;
    };

    // ******************************************************
    // get functions
    // ******************************************************
    public: double get_turnover_time(void) {
        return t_decay;
    };
    public: int get_nsteps_per_turnover_time(void) {
        return nsteps_per_turnover_time;
    };
    // ******************************************************

    // ******************************************************
    public: int init_single_realisation(
        const int ndim, const double L[3], const double k_min, const double k_max,
        const int spect_form, const double power_law_exp, const double angles_exp,
        const double sol_weight, const int random_seed) {
        // ******************************************************
        // Initialise the turbulence generator for a single turbulent realisation, e.g., for producing
        // turbulent initial conditions, with parameters specified as inputs to the function; see descriptions below.
        // ******************************************************
        // set internal parameters
        this->ndim = ndim;
        this->L[X] = L[X]; // Length of box in x; used for wavenumber conversion below
        this->L[Y] = L[Y]; // Length of box in y
        this->L[Z] = L[Z]; // Length of box in z
        stir_min = (k_min-DBL_EPSILON) * 2*M_PI / L[X]; // Minimum driving wavenumber <~  k_min * 2pi / Lx
        stir_max = (k_max+DBL_EPSILON) * 2*M_PI / L[X]; // Maximum driving wavenumber >~  k_max * 2pi / Lx
        this->spect_form = spect_form; // (0: band/rectangle/constant, 1: paraboloid, 2: power law)
        this->power_law_exp = power_law_exp; // power-law amplitude exponent (only if spect_form = 2, i.e., power law)
        this->angles_exp = angles_exp; // angles exponent (only if spect_form = 2, i.e., power law)
        this->sol_weight = sol_weight; // solenoidal weight (0: compressive, 0.5: natural mix, 1.0: solenoidal)
        this->random_seed = random_seed; // set random seed for this realisation
        seed = this->random_seed; // set random seed for this realisation
        OUvar = 1.0; // Ornstein-Uhlenbeck variance (the returned turbulent field needs to be re-normalised outside this call, so we just set this to 1)
        TurbGen_printf("===============================================================================\n");
        // this makes the rms of the turbulent field independent of the solenoidal weight (see Eq. 9 in Federrath et al. 2010)
        sol_weight_norm = sqrt(3.0/ndim)*sqrt(3.0)*1.0/sqrt(1.0-2.0*sol_weight+ndim*pow(sol_weight,2.0));
        // initialise modes
        init_modes();
        // initialise random phases
        OU_noise_init();
        // calculate solenoidal and compressive coefficients (aka, akb) from OUphases
        get_decomposition_coeffs();
        // print info
        print_info("single_realisation");
        TurbGen_printf("===============================================================================\n");
        return 0;
    }; // init_single_realisation


    // ******************************************************
    public: int init_driving(std::string parameter_file) {
        // ******************************************************
        // Initialise the turbulence generator and all relevant internal data structures by reading from 'parameter_file'.
        // This is used for driving turbulence (as opposed to init_single_realisation, which is for creating a single pattern).
        // ******************************************************
        // set parameter file
        this->parameter_file = parameter_file;
        // check if parameter file is present
        FILE * fp = fopen(parameter_file.c_str(), "r");
        if (fp == NULL) {
            printf("TurbGen: ERROR: cannot access parameter file '%s'.\n", parameter_file.c_str()); exit(-1); 
        } else {
            fclose(fp);
        }
        // read parameter file
        double k_driv, k_min, k_max, ampl_coeff;
        read_from_parameter_file("ndim", 'i', &ndim); // Number of spatial dimensions (1,2,3)
        read_from_parameter_file("Lx", 'd', &L[X]); // Length of box in x
        read_from_parameter_file("Lx", 'd', &L[Y]); // Length of box in y
        read_from_parameter_file("Ly", 'd', &L[Z]); // Length of box in z
        read_from_parameter_file("velocity", 'd', &velocity); // Target turbulent velocity dispersion
        read_from_parameter_file("k_driv", 'd', &k_driv); // driving wavenumber in 2pi/L; sets t_decay below
        read_from_parameter_file("k_min", 'd', &k_min); // min wavenumber in 2pi/L
        read_from_parameter_file("k_max", 'd', &k_max); // max wavenumber in 2pi/L
        read_from_parameter_file("sol_weight", 'd', &sol_weight); // solenoildal weight
        read_from_parameter_file("spect_form", 'i', &spect_form); // spectral form
        read_from_parameter_file("power_law_exp", 'd', &power_law_exp); // power-law exponent (if spect_form=2)
        read_from_parameter_file("angles_exp", 'd', &angles_exp); // angles sampling exponent (if spect_form=2)
        read_from_parameter_file("ampl_coeff", 'd', &ampl_coeff); // driving amplitude coefficient (to adjust to target velocity)
        read_from_parameter_file("random_seed", 'i', &random_seed); // random seed
        read_from_parameter_file("nsteps_per_turnover_time", 'i', &nsteps_per_turnover_time); // number of pattern updates per t_decay
        // define derived physical quantities
        stir_min = (k_min-DBL_EPSILON) * 2*M_PI / L[X]; // Minimum driving wavenumber <~  k_min * 2pi / Lx
        stir_max = (k_max+DBL_EPSILON) * 2*M_PI / L[X]; // Maximum driving wavenumber >~  k_max * 2pi / Lx
        t_decay = L[X] / k_driv / velocity;             // Auto-correlation time, t_turb = Lx / k_driv / velocity;
                                                        // i.e., turbulent turnover (crossing) time; with k_driv in units of 2pi/Lx
        energy = pow(ampl_coeff*velocity,3.0) / L[X];   // Energy input rate => driving amplitude ~ sqrt(energy/t_decay)
                                                        // Note that energy input rate ~ velocity^3 * L_box^-1
                                                        // ampl_coeff is the amplitude coefficient and needs to be
                                                        // adjusted to approach actual target velocity dispersion
        OUvar = sqrt(energy/t_decay);                   // Ornstein-Uhlenbeck variance
        dt = t_decay / nsteps_per_turnover_time;        // time step in OU process and for creating new driving pattern
        step = -1;                                      // set internal OU step to -1 for start-up
        seed = random_seed;                             // copy original seed into local seed;
                                                        // local seeds gets updated everytime the random number generator is called
        TurbGen_printf("===============================================================================\n");
        // this makes the rms of the turbulent field independent of the solenoidal weight (see Eq. 9 in Federrath et al. 2010)
        sol_weight_norm = sqrt(3.0/ndim)*sqrt(3.0)*1.0/sqrt(1.0-2.0*sol_weight+ndim*pow(sol_weight,2.0));
        // initialise modes
        init_modes();
        // initialise Ornstein-Uhlenbeck sequence
        OU_noise_init();
        // calculate solenoidal and compressive coefficients (aka, akb) from OUphases
        get_decomposition_coeffs();
        // print info
        print_info("driving");
        TurbGen_printf("===============================================================================\n");
        return 0;
    }; // init_driving


    // ******************************************************
    public: bool check_for_update(double time) {
        // ******************************************************
        // Update driving pattern based on input 'time'.
        // If it is 'time' to update the pattern, call OU noise update
        // and update the decomposition coefficients; otherwise, simply return.
        // ******************************************************
        int step_requested = floor(time / dt); // requested OU step number based on input 'time'
        if (verbose) TurbGen_printf("step_requested = %i\n", step_requested);
        if (step_requested <= step) {
            if (verbose) TurbGen_printf("no update of pattern...returning.\n");
            return false; // no update (yet) -> return false, i.e., no change of driving pattern
        }
        // if we are here: update OU vector
        for (int is = step; is < step_requested; is++) {
            OU_noise_update(); // this seeks to the requested OU state (updates OUphases)
            if (verbose) TurbGen_printf("step = %i, time = %f\n", step, step*dt);
        }
        get_decomposition_coeffs(); // calculate solenoidal and compressive coefficients (aka, akb) from OUphases
        double time_gen = step * dt;
        TurbGen_printf("Generated new turbulence driving pattern: #%6i, time = %e, time/t_turb = %-7.2f\n", step, time_gen, time_gen/t_decay);
        return true; // we just updated the driving pattern
    }; // check_for_update


    // ******************************************************
    public: void get_turb_vector_unigrid(const double pos_beg[3], const double pos_end[3], const int n[3], float * return_grid[3]) {
        // ******************************************************
        // Compute 3D physical turbulent vector field on a 3D uniform grid,
        // provided start coordinate pos_beg[3] and end coordinate pos_end[3]
        // of the grid in (x,y,x) and number of points in grid n[3].
        // Return into turbulent vector field into float * return_grid[3].
        // Note that index in return_grid[X][index] is looped with x (index i)
        // as the inner loop and with z (index k) as the outer loop.
        // ******************************************************
        if (verbose) TurbGen_printf("pos_beg = %f %f %f, pos_end = %f %f %f, n = %i %i %i\n",
                pos_beg[X], pos_beg[Y], pos_beg[Z], pos_end[X], pos_end[Y], pos_end[Z], n[X], n[Y], n[Z]);
        // containers for speeding-up calculations below
        std::vector<double> ampl(n_modes);
        std::vector< std::vector<double> > sinxi(n[X], std::vector<double>(n_modes));
        std::vector< std::vector<double> > cosxi(n[X], std::vector<double>(n_modes));
        std::vector< std::vector<double> > sinyj(n[Y], std::vector<double>(n_modes));
        std::vector< std::vector<double> > cosyj(n[Y], std::vector<double>(n_modes));
        std::vector< std::vector<double> > sinzk(n[Z], std::vector<double>(n_modes));
        std::vector< std::vector<double> > coszk(n[Z], std::vector<double>(n_modes));
        // compute dx, dy, dz
        double d[3];
        for (int dim = 0; dim < 3; dim++) d[dim] = (pos_end[dim] - pos_beg[dim]) / (n[dim]-1);
        // pre-compute grid position geometry, and trigonometry, for quicker access in loop over modes below
        for (int m = 0; m < n_modes; m++) { // loop over modes
            ampl[m] = 2.0 * sol_weight_norm * this->ampl[m]; // pre-compute amplitude including normalisation factors
            for (int i = 0; i < n[X]; i++) {
                sinxi[i][m] = sin(mode[X][m]*(pos_beg[X]+i*d[X]));
                cosxi[i][m] = cos(mode[X][m]*(pos_beg[X]+i*d[X]));
            }
            for (int j = 0; j < n[Y]; j++) {
                sinyj[j][m] = sin(mode[Y][m]*(pos_beg[Y]+j*d[Y]));
                cosyj[j][m] = cos(mode[Y][m]*(pos_beg[Y]+j*d[Y]));
            }
            for (int k = 0; k < n[Z]; k++) {
                sinzk[k][m] = sin(mode[Z][m]*(pos_beg[Z]+k*d[Z]));
                coszk[k][m] = cos(mode[Z][m]*(pos_beg[Z]+k*d[Z]));
            }
        }
        // scratch variables
        double v[3];
        double real, imag;
        // loop over cells in return_grid
        for (int k = 0; k < n[Z]; k++) {
            for (int j = 0; j < n[Y]; j++) {
                for (int i = 0; i < n[X]; i++) {
                    // clear
                    v[X] = 0.0; v[Y] = 0.0; v[Z] = 0.0;
                    // loop over modes
                    for (int m = 0; m < n_modes; m++) {
                        // these are the real and imaginary parts, respectively, of
                        //  e^{ i \vec{k} \cdot \vec{x} } = cos(kx*x + ky*y + kz*z) + i sin(kx*x + ky*y + kz*z)
                        real =  ( cosxi[i][m]*cosyj[j][m] - sinxi[i][m]*sinyj[j][m] ) * coszk[k][m] -
                                ( sinxi[i][m]*cosyj[j][m] + cosxi[i][m]*sinyj[j][m] ) * sinzk[k][m];
                        imag =  ( cosyj[j][m]*sinzk[k][m] + sinyj[j][m]*coszk[k][m] ) * cosxi[i][m] +
                                ( cosyj[j][m]*coszk[k][m] - sinyj[j][m]*sinzk[k][m] ) * sinxi[i][m];
                        // accumulate total v as sum over modes
                        v[X] += ampl[m] * (aka[X][m]*real - akb[X][m]*imag);
                        v[Y] += ampl[m] * (aka[Y][m]*real - akb[Y][m]*imag);
                        v[Z] += ampl[m] * (aka[Z][m]*real - akb[Z][m]*imag);
                    }
                    // copy into return grid
                    long index = k*n[X]*n[Y] + j*n[X] + i;
                    return_grid[X][index] = v[X];
                    return_grid[Y][index] = v[Y];
                    return_grid[Z][index] = v[Z];
                } // i
            } // j
        } // k
    } // get_turb_vector_unigrid


    // ******************************************************
    public: void get_turb_vector(const double pos[3], double v[3]) {
        // ******************************************************
        // Compute physical turbulent vector v[3]=(vx,vy,vz) at position pos[3]=(x,y,z)
        // from loop over all turbulent modes; return into double v[3]
        // ******************************************************
        // containers for speeding-up calculations below
        std::vector<double> ampl(n_modes);
        std::vector<double> sinx(n_modes); std::vector<double> cosx(n_modes);
        std::vector<double> siny(n_modes); std::vector<double> cosy(n_modes);
        std::vector<double> sinz(n_modes); std::vector<double> cosz(n_modes);
        // pre-compute some trigonometry
        for (int m = 0; m < n_modes; m++) {
            ampl[m] = 2.0 * sol_weight_norm * this->ampl[m]; // pre-compute amplitude including normalisation factors
            sinx[m] = sin(mode[X][m]*pos[X]);
            cosx[m] = cos(mode[X][m]*pos[X]);
            siny[m] = sin(mode[Y][m]*pos[Y]);
            cosy[m] = cos(mode[Y][m]*pos[Y]);
            sinz[m] = sin(mode[Z][m]*pos[Z]);
            cosz[m] = cos(mode[Z][m]*pos[Z]);
        }
        // scratch variables
        double real, imag;
        // init return vector with zero
        v[X] = 0.0; v[Y] = 0.0; v[Z] = 0.0;
        // loop over modes
        for (int m = 0; m < n_modes; m++) {
            // these are the real and imaginary parts, respectively, of
            //  e^{ i \vec{k} \cdot \vec{x} } = cos(kx*x + ky*y + kz*z) + i sin(kx*x + ky*y + kz*z)
            real = ( cosx[m]*cosy[m] - sinx[m]*siny[m] ) * cosz[m] - ( sinx[m]*cosy[m] + cosx[m]*siny[m] ) * sinz[m];
            imag = cosx[m] * ( cosy[m]*sinz[m] + siny[m]*cosz[m] ) + sinx[m] * ( cosy[m]*cosz[m] - siny[m]*sinz[m] );
            // return vector for this position x, y, z
            v[X] += ampl[m] * (aka[X][m]*real - akb[X][m]*imag);
            v[Y] += ampl[m] * (aka[Y][m]*real - akb[Y][m]*imag);
            v[Z] += ampl[m] * (aka[Z][m]*real - akb[Z][m]*imag);
        }
    }; // get_turb_vector


    // ******************************************************
    private: void print_info(std::string print_mode) {
        // ******************************************************
        if (print_mode == "driving") {
            TurbGen_printf("Initialized %i modes for turbulence driving based on parameter file '%s'.\n", n_modes, parameter_file.c_str());
        }
        if (print_mode == "single_realisation") {
            TurbGen_printf("Initialized %i modes for generating a single turbulent realisation.\n", n_modes);
        }
        if (spect_form == 0) TurbGen_printf(" spectral form                                       = %i (Band)\n", spect_form);
        if (spect_form == 1) TurbGen_printf(" spectral form                                       = %i (Parabola)\n", spect_form);
        if (spect_form == 2) TurbGen_printf(" spectral form                                       = %i (Power Law)\n", spect_form);
        if (spect_form == 2) TurbGen_printf(" power-law exponent                                  = %e\n", power_law_exp);
        if (spect_form == 2) TurbGen_printf(" power-law angles sampling exponent                  = %e\n", angles_exp);
        TurbGen_printf(" box size Lx                                         = %e\n", L[X]);
        if (print_mode == "driving") {
            TurbGen_printf(" turbulent velocity dispersion                       = %e\n", velocity);
            TurbGen_printf(" turbulent turnover (auto-correlation) time          = %e\n", t_decay);
            TurbGen_printf("  -> characteristic turbulent wavenumber (in 2pi/Lx) = %e\n", L[X] / velocity / t_decay);
        }
        TurbGen_printf(" minimum wavenumber (in 2pi/Lx)                      = %e\n", stir_min / (2*M_PI) * L[X]);
        TurbGen_printf(" maximum wavenumber (in 2pi/Lx)                      = %e\n", stir_max / (2*M_PI) * L[X]);
        if (print_mode == "driving") {
            TurbGen_printf(" amplitude coefficient                               = %e\n", pow(energy*L[X],1.0/3.0) / velocity);
            TurbGen_printf("  -> driving energy (injection rate)                 = %e\n", energy);
        }
        TurbGen_printf(" solenoidal weight (0.0: comp, 0.5: mix, 1.0: sol)   = %e\n", sol_weight);
        TurbGen_printf("  -> solenoidal weight norm (set based on Ndim = %i)  = %e\n", ndim, sol_weight_norm);
        TurbGen_printf(" random seed                                         = %i\n", random_seed);
    }; // print_info


    // ******************************************************
    private: int read_from_parameter_file(std::string search, char type, void * ret) {
        // ******************************************************
        // parse each line in turbulence generator 'parameter_file' and search for 'search'
        // at the beginning of each line; if 'search' is found return double of value after '='
        // type: 'i' for int, 'd' for double return type void *ret
        // ******************************************************
        FILE * fp;
        char * line = NULL;
        size_t len = 0;
        ssize_t read;
        fp = fopen(parameter_file.c_str(), "r");
        std::string msg = ClassSignature+"ERROR: could not open parameter file '%s'\n";
        if (fp == NULL) { printf(msg.c_str(), parameter_file.c_str()); exit(-1); }
        bool found = false;
        while ((read = getline(&line, &len, fp)) != -1) {
            if (strncmp(line, search.c_str(), strlen(search.c_str())) == 0) {
                if (verbose) TurbGen_printf("line = '%s'\n", line);
                char * substr1 = strstr(line, "="); // extract everything after (and including) '='
                if (verbose) TurbGen_printf("substr1 = '%s'\n", substr1);
                char * substr2 = strstr(substr1, "!"); // deal with comment '! ...'
                char * substr3 = strstr(substr1, "#"); // deal with comment '# ...'
                int end_index = strlen(substr1);
                if ((substr2 != NULL) && (substr3 != NULL)) { // if comment is present, reduce end_index
                    end_index -= std::max(strlen(substr2),strlen(substr3));
                } else { // if comment is present, reduce end_index
                    if (substr2 != NULL) end_index -= strlen(substr2);
                    if (substr3 != NULL) end_index -= strlen(substr3);
                }
                char dest[100]; memset(dest, '\0', sizeof(dest));
                strncpy(dest, substr1+1, end_index-1);
                if (verbose) TurbGen_printf("dest = '%s'\n", dest);
                if (type == 'i') *(int*)(ret) = atoi(dest);
                if (type == 'd') *(double*)(ret) = atof(dest);
                found = true;
            }
            if (found) break;
        }
        fclose(fp);
        if (line) free(line);
        if (found) return 0; else {
            std::string msg = ClassSignature+"ERROR: requested parameter '%s' not found in file '%s'\n";
            printf(msg.c_str(), search.c_str(), parameter_file.c_str());
            exit(-1);
        }
    }; // read_from_parameter_file


    // ******************************************************
    private: int init_modes(void) {
        // ******************************************************
        // initialise all turbulent modes information
        // ******************************************************

        int ikmin[3], ikmax[3], ik[3], tot_n_modes;
        double k[3], ka, kc, amplitude, parab_prefact;

        // applies in case of power law (spect_form == 2)
        int iang, nang;
        double rand, phi, theta;

        // this is for spect_form = 1 (paraboloid) only
        // prefactor for amplitude normalistion to 1 at kc = 0.5*(stir_min+stir_max)
        parab_prefact = -4.0 / pow(stir_max-stir_min,2.0);

        // characteristic k for scaling the amplitude below
        kc = stir_min;
        if (spect_form == 1) kc = 0.5*(stir_min+stir_max);

        ikmin[X] = 0;
        ikmin[Y] = 0;
        ikmin[Z] = 0;

        ikmax[X] = 256;
        ikmax[Y] = 0;
        ikmax[Z] = 0;
        if (ndim > 1) ikmax[Y] = 256;
        if (ndim > 2) ikmax[Z] = 256;

        // determine the number of required modes (in case of full sampling)
        n_modes = 0;
        for (ik[X] = ikmin[X]; ik[X] <= ikmax[X]; ik[X]++) {
            k[X] = 2*M_PI * ik[X] / L[X];
            for (ik[Y] = ikmin[Y]; ik[Y] <= ikmax[Y]; ik[Y]++) {
                k[Y] = 2*M_PI * ik[Y] / L[Y];
                for (ik[Z] = ikmin[Z]; ik[Z] <= ikmax[Z]; ik[Z]++) {
                    k[Z] = 2*M_PI * ik[Z] / L[Z];
                    ka = sqrt( k[X]*k[X] + k[Y]*k[Y] + k[Z]*k[Z] );
                    if ((ka >= stir_min) && (ka <= stir_max)) {
                        n_modes++;
                        if (ndim > 1) n_modes += 1;
                        if (ndim > 2) n_modes += 2;
                    }
                }
            }
        }
        tot_n_modes = n_modes;
        if (spect_form != 2) { // for Band (spect_form=0) and Parabola (spect_form=1)
            if (tot_n_modes > NameSpaceTurbGen::tgd_max_n_modes) {
                TurbGen_printf(" n_modes = %i, maxmodes = %i", n_modes, NameSpaceTurbGen::tgd_max_n_modes);
                TurbGen_printf("Too many stirring modes");
                exit(-1);
            }
            TurbGen_printf("Generating %i turbulent modes...\n", tot_n_modes);
        }

        n_modes = 0; // reset

        // ===================================================================
        // === for band and parabolic spectrum, use the standard full sampling
        if (spect_form != 2) {

            // loop over all kx, ky, kz to generate turbulent modes
            for (ik[X] = ikmin[X]; ik[X] <= ikmax[X]; ik[X]++) {
                k[X] = 2*M_PI * ik[X] / L[X];
                for (ik[Y] = ikmin[Y]; ik[Y] <= ikmax[Y]; ik[Y]++) {
                    k[Y] = 2*M_PI * ik[Y] / L[Y];
                        for (ik[Z] = ikmin[Z]; ik[Z] <= ikmax[Z]; ik[Z]++) {
                        k[Z] = 2*M_PI * ik[Z] / L[Z];

                        ka = sqrt( k[X]*k[X] + k[Y]*k[Y] + k[Z]*k[Z] );

                        if ((ka >= stir_min) && (ka <= stir_max)) {

                            if (spect_form == 0) amplitude = 1.0;                                    // Band
                            if (spect_form == 1) amplitude = fabs(parab_prefact*pow(ka-kc,2.0)+1.0); // Parabola

                            // note: power spectrum ~ amplitude^2 (1D), amplitude^2 * 2pi k (2D), amplitude^2 * 4pi k^2 (3D) 
                            amplitude = sqrt(amplitude) * pow(kc/ka,(ndim-1)/2.0);

                            n_modes++;
                            ampl.push_back(amplitude);
                            mode[X].push_back(k[X]);
                            mode[Y].push_back(k[Y]);
                            mode[Z].push_back(k[Z]);

                            if (ndim > 1) {
                                n_modes++;
                                ampl.push_back(amplitude);
                                mode[X].push_back( k[X]);
                                mode[Y].push_back(-k[Y]);
                                mode[Z].push_back( k[Z]);
                            }

                            if (ndim > 2) {
                                n_modes++;
                                ampl.push_back(amplitude);
                                mode[X].push_back( k[X]);
                                mode[Y].push_back( k[Y]);
                                mode[Z].push_back(-k[Z]);
                                n_modes++;
                                ampl.push_back(amplitude);
                                mode[X].push_back( k[X]);
                                mode[Y].push_back(-k[Y]);
                                mode[Z].push_back(-k[Z]);
                            }

                            if ((n_modes) % 1000 == 0) TurbGen_printf(" ... %i of total %i modes generated...\n", n_modes, tot_n_modes);

                        } // in k range
                    } // ikz
                } // iky
            } // ikx
        } // spect_form != 2

        // ===============================================================================
        // === for power law (spect_form=2), generate modes that are distributed randomly on the k-sphere
        // === with the number of angles growing ~ k^angles_exp
        if (spect_form == 2) {

            TurbGen_printf("There would be %i turbulent modes, if k-space were fully sampled (angles_exp = 2.0)...\n", tot_n_modes);
            TurbGen_printf("Here we are using angles_exp = %f\n", angles_exp);

            // initialize additional random numbers (uniformly distributed) to randomise angles
            int seed_init = -seed; // initialise Numerical Recipes rand gen (call with negative integer)
            rand = ran2(&seed_init);

            // loop between smallest and largest k
            ikmin[0] = std::max(1, (int)round(stir_min*L[X]/(2*M_PI)));
            ikmax[0] =             (int)round(stir_max*L[X]/(2*M_PI));

            TurbGen_printf("Generating turbulent modes within k = [%i, %i]\n", ikmin[0], ikmax[0]);

            for (ik[0] = ikmin[0]; ik[0] <= ikmax[0]; ik[0]++) {

                nang = pow(2.0,ndim) * ceil(pow((double)ik[0],angles_exp));
                TurbGen_printf("ik, number of angles = %i, %i\n", ik[0], nang);

                for (iang = 1; iang <= nang; iang++) {

                    phi = 2*M_PI * ran2(&seed); // phi = [0,2pi] sample the whole sphere
                    if (ndim == 1) {
                        if (phi <  M_PI) phi = 0.0;
                        if (phi >= M_PI) phi = M_PI;
                    }
                    theta = M_PI/2.0;
                    if (ndim > 2) theta = acos(1.0 - 2.0*ran2(&seed)); // theta = [0,pi] sample the whole sphere

                    if (verbose) TurbGen_printf("entering: theta = %f, phi = %f\n", theta, phi);

                    rand = ik[0] + ran2(&seed) - 0.5;
                    k[X] = 2*M_PI * round(rand*sin(theta)*cos(phi)) / L[X];
                    if (ndim > 1)
                        k[Y] = 2*M_PI * round(rand*sin(theta)*sin(phi)) / L[Y];
                    else
                        k[Y] = 0.0;
                    if (ndim > 2)
                        k[Z] = 2*M_PI * round(rand*cos(theta)) / L[Z];
                    else
                        k[Z] = 0.0;

                    ka = sqrt( k[X]*k[X] + k[Y]*k[Y] + k[Z]*k[Z] );

                    if ((ka >= stir_min) && (ka <= stir_max)) {

                        if (n_modes > NameSpaceTurbGen::tgd_max_n_modes) {
                            TurbGen_printf(" n_modes = %i, maxmodes = %i", n_modes, NameSpaceTurbGen::tgd_max_n_modes);
                            TurbGen_printf("Too many stirring modes");
                            exit(-1);
                        }

                        amplitude = pow(ka/kc,power_law_exp); // Power law

                        // note: power spectrum ~ amplitude^2 (1D), amplitude^2 * 2pi k (2D), amplitude^2 * 4pi k^2 (3D)
                        // ...and correct for the number of angles sampled relative to the full sampling (k^2 per k-shell in 3D)
                        amplitude = sqrt( amplitude * pow((double)ik[0],ndim-1) / (double)(nang) * 4.0*sqrt(3.0) ) * pow(kc/ka,(ndim-1)/2.0);

                        n_modes++;
                        ampl.push_back(amplitude);
                        mode[X].push_back(k[X]);
                        mode[Y].push_back(k[Y]);
                        mode[Z].push_back(k[Z]);

                        if ((n_modes) % 1000 == 0) TurbGen_printf(" ... %i modes generated...\n", n_modes);

                    } // in k range

                } // loop over angles
            } // loop over k
        } // spect_form == 2

        return 0;
    }; // init_modes


    // ******************************************************
    private: void OU_noise_init(void) {
        // ******************************************************
        // initialize pseudo random sequence for the Ornstein-Uhlenbeck (OU) process
        // ******************************************************
        OUphases.resize(6*n_modes);
        for (int i = 0; i < 6*n_modes; i++) OUphases[i] = OUvar * get_random_number();
    }; // OU_noise_init


    // ******************************************************
    private: void OU_noise_update(void) {
        // ******************************************************
        // update Ornstein-Uhlenbeck sequence
        //
        // The sequence x_n is a Markov process that takes the previous value,
        // weights by an exponential damping factor with a given correlation
        // time 'ts', and drives by adding a Gaussian random variable with
        // variance 'variance', weighted by a second damping factor, also
        // with correlation time 'ts'. For a timestep of dt, this sequence
        // can be written as
        //
        //     x_n+1 = f x_n + sigma * sqrt (1 - f**2) z_n,
        //
        // where f = exp (-dt / ts), z_n is a Gaussian random variable drawn
        // from a Gaussian distribution with unit variance, and sigma is the
        // desired variance of the OU sequence.
        //
        // The resulting sequence should satisfy the properties of zero mean,
        // and stationarity (independent of portion of sequence) RMS equal to
        // 'variance'. Its power spectrum in the time domain can vary from
        // white noise to "brown" noise (P (f) = const. to 1 / f^2).
        //
        // References:
        //   Bartosch (2001)
        //   Eswaran & Pope (1988)
        //   Schmidt et al. (2009)
        //   Federrath et al. (2010, A&A 512, A81); Eq. (4)
        //
        // ******************************************************
        const double damping_factor = exp(-dt/t_decay);
        for (int i = 0; i < 6*n_modes; i++) {
            OUphases[i] = OUphases[i] * damping_factor + 
                        sqrt(1.0 - damping_factor*damping_factor) * OUvar * get_random_number();
        }
        step++; // update internal OU step number
    }; // OU_noise_update


    // ******************************************************
    private: void get_decomposition_coeffs(void) {
        // ******************************************************
        // This routine applies the projection operator based on the OU phases.
        // See Eq. (6) in Federrath et al. (2010).
        // ******************************************************
        for (int d = 0; d < ndim; d++) {
            aka[d].resize(n_modes);
            akb[d].resize(n_modes);
        }
        double ka, kb, kk, diva, divb, curla, curlb;
        for (int m = 0; m < n_modes; m++) {
            ka = 0.0;
            kb = 0.0;
            kk = 0.0;
            for (int d = 0; d < ndim; d++) {
                kk = kk + mode[d][m]*mode[d][m];
                ka = ka + mode[d][m]*OUphases[6*m+2*d+1];
                kb = kb + mode[d][m]*OUphases[6*m+2*d+0];
            }
            for (int d = 0; d < ndim; d++) {
                diva  = mode[d][m]*ka/kk;
                divb  = mode[d][m]*kb/kk;
                curla = OUphases[6*m+2*d+0] - divb;
                curlb = OUphases[6*m+2*d+1] - diva;
                aka[d][m] = sol_weight*curla+(1.0-sol_weight)*divb;
                akb[d][m] = sol_weight*curlb+(1.0-sol_weight)*diva;
                // purely compressive
                // aka[d][m] = mode[d][m]*kb/kk;
                // akb[d][m] = mode[d][m]*ka/kk;
                // purely solenoidal
                // aka[d][m] = R - mode[d][m]*kb/kk;
                // akb[d][m] = I - mode[d][m]*ka/kk;
                if (verbose) {
                    TurbGen_printf("mode(dim=%1i, mode=%3i) = %12.6f\n", d, m, mode[d][m]);
                    TurbGen_printf("aka (dim=%1i, mode=%3i) = %12.6f\n", d, m, aka [d][m]);
                    TurbGen_printf("akb (dim=%1i, mode=%3i) = %12.6f\n", d, m, akb [d][m]);
                    TurbGen_printf("ampl(dim=%1i, mode=%3i) = %12.6f\n", d, m, ampl   [m]);
                }
            }
        }
    }; // get_decomposition_coeffs


    // ******************************************************
    private: double get_random_number(void) {
        // ******************************************************
        //  get random number; draws a number randomly from a Gaussian distribution
        //  with the standard uniform distribution function "random_number"
        //  using the Box-Muller transformation in polar coordinates. The
        //  resulting Gaussian has unit variance.
        // ******************************************************
        double r1 = ran1s(&seed);
        double r2 = ran1s(&seed);
        double g1 = sqrt(2.0*log(1.0/r1))*cos(2*M_PI*r2);
        return g1;
    }; // get_random_number


    // ************** Numerical recipes ran1s ***************
    private: double ran1s(int * idum) {
        // ******************************************************
        static const int IA=16807, IM=2147483647, IQ=127773, IR=2836;
        static const double AM=1.0/IM, RNMX=1.0-1.2e-7;
        if (*idum <= 0) *idum = std::max(-*idum, 1);
        int k = *idum/IQ;
        *idum = IA*(*idum-k*IQ)-IR*k;
        if (*idum < 0) *idum = *idum+IM;
        int iy = *idum;
        double ret = std::min(AM*iy, RNMX);
        return ret;
    }; // ran1s


    // ************** Numerical recipes ran2 ****************
    private: double ran2(int * idum) {
        // ******************************************************
        // Long period (> 2 x 10^18) random number generator of L'Ecuyer
        // with Bays-Durham shuffle and added safeguards.
        // Returns a uniform random deviate between 0.0 and 1.0 (exclusive of the endpoint values).
        // Call with idum a negative integer to initialize; thereafter,
        // do not alter idum between successive deviates in a sequence.
        // RNMX should approximate the largest floating value that is less than 1.
        // ******************************************************
        static const int IM1=2147483563, IM2=2147483399, IMM1=IM1-1, IA1=40014, IA2=40692, IQ1=53668,
                        IQ2=52774, IR1=12211, IR2=3791, NTAB=32, NDIV=1+IMM1/NTAB;
        static const double AM=1.0/IM1, RNMX=1.0-1.2e-7;
        static int idum2 = 123456789;
        static int iy = 0;
        static int iv[NTAB];
        int j, k;
        if (*idum <= 0) {
            *idum = std::max(-*idum, 1);
            idum2 = *idum;
            for (j = NTAB+7; j >= 0; j--) {
                k = *idum/IQ1;
                *idum = IA1*(*idum-k*IQ1)-k*IR1;
                if (*idum < 0) *idum += IM1;
                if (j < NTAB) iv[j] = *idum;
            }
            iy = iv[0];
        }
        k = *idum/IQ1;
        *idum = IA1*(*idum-k*IQ1)-k*IR1;
        if (*idum < 0) *idum += IM1;
        k = idum2/IQ2;
        idum2 = IA2*(idum2-k*IQ2)-k*IR2;
        if (idum2 < 0) idum2 += IM2;
        j = iy/NDIV;
        iy = iv[j]-idum2;
        iv[j] = *idum;
        if (iy < 1) iy += IMM1;
        double ret = std::min(AM*iy, RNMX);
        return ret;
    }; // ran2


    // ******************************************************
    private: void TurbGen_printf(std::string format, ...) {
        // ******************************************************
        // special printf prepends string and only lets PE=0 print
        // ******************************************************
        va_list args;
        va_start(args, format);
        std::string new_format = ClassSignature+format;
        if (PE == 0) vfprintf(stdout, new_format.c_str(), args);
        va_end(args);
    }; // TurbGen_printf

}; // end class TurbGen

#endif
// end of TURBULENCE_GENERATOR_H
