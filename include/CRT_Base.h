/* * ATUS2 - The ATUS2 package is atom interferometer Toolbox developed at ZARM
 * (CENTER OF APPLIED SPACE TECHNOLOGY AND MICROGRAVITY), Germany. This project is
 * founded by the DLR Agentur (Deutsche Luft und Raumfahrt Agentur). Grant numbers:
 * 50WM0942, 50WM1042, 50WM1342.
 * Copyright (C) 2017 Želimir Marojević, Ertan Göklü, Claus Lämmerzahl
 *
 * This file is part of ATUS2.
 *
 * ATUS2 is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * ATUS2 is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with ATUS2.  If not, see <http://www.gnu.org/licenses/>.
 */


#include <ostream>
#include <fstream>
#include <string>
#include <cstring>
#include <array>

#include "strtk.hpp"
#include "CRT_shared.h"
#include "cft_base.h"
#include "ParameterHandler.h"

using namespace std;

#ifndef __class_CRT_Base__
#define __class_CRT_Base__

/** Template Class for the propagation in <B>dim</B> dimensions of a BEC with <B>no_int_states</B> internal states
  *
  * In this template class functions for the propagation of a BEC are defined.
  * The two following cases can be computed:
  *   - Propagation in the absence of external fields
  *   - Propagation in the presence of external diagonal fields
  */
template <class T, int dim, int no_int_states>
class CRT_Base : public CRT_shared
{
public:
  CRT_Base( ParameterHandler * );
  virtual ~CRT_Base();

  double Get_Particle_Number(const int comp=0);

  void run_sequence();

  void Setup_Momentum( CPoint<dim>, const int comp=0 );
  void Expval_Position( CPoint<dim> &, const int comp=0 );
  void Expval_Momentum( CPoint<dim> &, const int comp=0 );
  void Save( double *, std::string );
  void Save( fftw_complex *, std::string );
  void Save_Phi( std::string, const int comp=0 );
  void Append_Phi( std::string, const int comp=0 );
  void Dump_2( ofstream & );

  void Set_custom_fct( StepFunction &fct)
  {
    m_custom_fct=fct;
  };

  void Init_Potential();
  void Setup_Potential( const int, const int, const double );

protected:
  static void Do_FT_Step_full_Wrapper(void *,sequence_item &);
  static void Do_FT_Step_half_Wrapper(void *,sequence_item &);
  static void Do_NL_Step_Wrapper(void *,sequence_item &);
  static void Do_NL_Step_Wrapper_one(void *,sequence_item &);

  void Do_FT_Step_full();
  void Do_FT_Step_half();
  void Do_NL_Step();

  /// Object for reading from xml files
  ParameterHandler *m_params;

  /// Dimensionless scaling factor for the kinetic part in n dimensions
  CPoint<dim> m_alpha;

  /// Exponential of the whole kinetic operator. See Init() for further information.
  fftw_complex *m_full_step;
  /// Exponential of half of the kinetic operator. See Init() for further information.
  fftw_complex *m_half_step;

  void Init();
  void Allocate();
  void LoadFiles();

  bool m_potenial_initialized;

  virtual bool run_custom_sequence( const sequence_item & )=0;

  /** Array of dimensionsless scaling factors for the nonlinear term in the GPE
    *
    * In case of two internal states the elements of m_gs represents the following matrix:
    * \f[
    *   \begin{pmatrix}
    *     g_{11} & g_{12} \\
    *     g_{21} & g_{22}
    *   \end{pmatrix}
    * \f]
    */
  std::array<double,no_int_states *no_int_states> m_gs;

  /** Array of internal states of the wavefunction
    *
    * T is a class of type cft (complex wavefunction) or rft (real wavefunction).
    * @see Fourier namespace
    */
  std::array<T *,no_int_states> m_fields;

  ///time independent external potentials
  std::array<vector<double>,no_int_states> m_Potential;

  ///Map between String (name of functions) and StepFunction
  std::map<std::string,StepFunction> m_map_stepfcts;
  ///StepFunction for custom functions
  StepFunction m_custom_fct;
};

/** Constructor
  *
  * The scaling factor m_alpha for the kinetic part and the scaling factor m_gs for the nonlinear term are initialised.
  * Call functions Allocate(), LoadFiles() and Init().
  * @param params Pointer to ParameterHandler object to read from xml files
  */
template <class T, int dim, int no_int_states>
CRT_Base<T,dim,no_int_states>::CRT_Base( ParameterHandler *params )
{
  m_params = params;

  //Read header from file specified by FILENAME in xml-file
  Read_header(params->Get_simulation("FILENAME"),dim);
  assert( m_header.nDims == dim );

  Allocate();
  LoadFiles();
  Init();

  // Map between "half_step" and Do_FT_Step_half
  m_map_stepfcts["half_step"] = &Do_FT_Step_half_Wrapper;
  m_map_stepfcts["full_step"] = &Do_FT_Step_full_Wrapper;
  m_map_stepfcts["freeprop"] = &Do_NL_Step_Wrapper;
  m_map_stepfcts["freeprop_lin"] = &Do_NL_Step_Wrapper_one;
  m_custom_fct=nullptr;
  m_potenial_initialized=false;

  string tmpstr;

  //Read alpha from xml
  for ( int i=0; i<dim; i++ )
    m_alpha[i] = m_params->Get_VConstant( "Alpha_1", i );;
  m_header.dt = params->Get_dt();
}

/// Destructor
template <class T, int dim, int no_int_states>
CRT_Base<T,dim,no_int_states>::~CRT_Base()
{
  for ( int i=0; i<no_int_states; i++ )
    delete m_fields[i];
  fftw_free( m_full_step );
  fftw_free( m_half_step );
}

/** Allocate m_fields, m_full_step and m_half_step
  */
template <class T, int dim, int no_int_states>
void CRT_Base<T,dim,no_int_states>::Allocate()
{
  for ( int i=0; i<no_int_states; i++ )
  {
    m_fields[i] = new T( m_header );
    m_fields[i]->SetFix(false);
  }

  m_full_step = (fftw_complex *)fftw_malloc( sizeof(fftw_complex)*m_no_of_pts );
  m_half_step = (fftw_complex *)fftw_malloc( sizeof(fftw_complex)*m_no_of_pts );
}

/** Load initial wavefunctions from files
  *
  * The filenames are defined in the xml file
  */
template <class T, int dim, int no_int_states>
void CRT_Base<T,dim,no_int_states>::LoadFiles()
{
  ifstream in;

  //File 1
  in.open( m_params->Get_simulation("FILENAME"), ifstream::binary );
  if ( in.is_open() )
  {
    in.seekg( sizeof(generic_header), ifstream::beg );
    in.read( (char *)m_fields[0]->Getp2In(), sizeof(fftw_complex)*m_no_of_pts );
    in.close();
  }
  else
  {
    throw string("Could not open file " + m_params->Get_simulation("FILENAME") + "\n");
  }

  // File 2,...
  for ( int i=1; i<no_int_states; i++ )
  {
    string str = "FILENAME_" + to_string(i+1);
    in.open( m_params->Get_simulation(str), ifstream::binary );
    if ( in.is_open() )
    {
      in.seekg( sizeof(generic_header), ifstream::beg );
      in.read( (char *)m_fields[i]->Getp2In(), sizeof(fftw_complex)*m_no_of_pts );
      in.close();
    }
    else
    {
      throw string("Could not open file " + m_params->Get_simulation(str) + "\n");
    }
  }
}

/** The exponential of the kinetic operator in momentum space is calculated according to the operator splitting method.
  *
  * The exponential of half of the kinetic operator is given by
  * \f[
  *   \exp \left(-i\frac{\Delta t}{2}\hat{K}\right) = \exp \left(-i\frac{\Delta t}{2}k^2 \alpha\right).
  * \f]
  * We call the solution of this exponential m_half_step.
  *
  * If we compute the whole kinetic operator we call this m_full_step
  */
template <class T, int dim, int no_int_states>
void CRT_Base<T,dim,no_int_states>::Init()
{
  #pragma omp parallel
  {
    const double dt = -m_header.dt;
    double phi;

    CPoint<dim> k;

    #pragma omp for
    for ( int i=0; i<m_no_of_pts; i++ )
    {
      k = m_fields[0]->Get_k(i);
      phi = dt*(k.scale(m_alpha)*k);

      m_half_step[i][0] = cos(0.5*phi);
      m_half_step[i][1] = sin(0.5*phi);
      m_full_step[i][0] = cos(phi);
      m_full_step[i][1] = sin(phi);
    }
  }
}

template <class T, int dim, int no_int_states>
void CRT_Base<T,dim,no_int_states>::Init_Potential()
{
  for ( auto &it : m_Potential )
  {
    it.resize(m_no_of_pts,0);
  }
  m_potenial_initialized = true;
}

template <class T, int dim, int no_int_states>
void CRT_Base<T,dim,no_int_states>::Setup_Potential( const int comp, const int index, const double val )
{
  if ( m_potenial_initialized == false )
  {
    std::cerr << "Potential not initialized. You forgot to invoke Init_Potential." << std::endl;
    throw;
  }
  assert( comp > -1 && comp < no_int_states );
  assert( index > -1 && comp < m_no_of_pts );

  m_Potential[comp][index] = val;
}

/** Wrapper function for Do_FT_Step_full()
  * @param ptr Function pointer to be set to Do_FT_Step_full()
  * @param seq Additional information about the sequence (for example file names if a file has to be read)
  */
template <class T, int dim, int no_int_states>
void CRT_Base<T,dim,no_int_states>::Do_FT_Step_full_Wrapper ( void *ptr, sequence_item &seq )
{
  CRT_Base<T,dim,no_int_states> *self = static_cast<CRT_Base<T,dim,no_int_states>*>(ptr);
  self->Do_FT_Step_full();
}

/** Wrapper function for Do_Step_half()
  * @param ptr Function pointer to be set to Do_FT_Step_half()
  * @param seq Additional information about the sequence (for example file names if a file has to be read)
  */
template <class T, int dim, int no_int_states>
void CRT_Base<T,dim,no_int_states>::Do_FT_Step_half_Wrapper ( void *ptr, sequence_item &seq )
{
  CRT_Base<T,dim,no_int_states> *self = static_cast<CRT_Base<T,dim,no_int_states>*>(ptr);
  self->Do_FT_Step_half();
}

template <class T, int dim, int no_int_states>
void CRT_Base<T,dim,no_int_states>::Do_NL_Step_Wrapper ( void *ptr, sequence_item &seq )
{
  CRT_Base<T,dim,no_int_states> *self = static_cast<CRT_Base<T,dim,no_int_states>*>(ptr);
  self->Do_NL_Step();
}

template <class T, int dim, int no_int_states>
void CRT_Base<T,dim,no_int_states>::Do_NL_Step_Wrapper_one ( void *ptr, sequence_item &seq )
{
}

/** Computes the full kinetic part
  */
template <class T, int dim, int no_int_states>
void CRT_Base<T,dim,no_int_states>::Do_FT_Step_full()
{
  //Fourier transform
  for ( int c=0; c<no_int_states; c++ )
    m_fields[c]->ft(-1);

  double tmp1;

  for ( int i=0; i<no_int_states; i++ )
  {
    fftw_complex *Psi = m_fields[i]->Getp2In();

    #pragma omp parallel for private(tmp1)
    for ( int l=0; l<m_no_of_pts; l++ )
    {
      tmp1 = Psi[l][0];
      Psi[l][0] = Psi[l][0]*m_full_step[l][0] - Psi[l][1]*m_full_step[l][1];
      Psi[l][1] = Psi[l][1]*m_full_step[l][0] + tmp1*m_full_step[l][1];
    }
  }
  //Fourier transform back into real space
  for ( int c=0; c<no_int_states; c++ )
    m_fields[c]->ft(1);
  //Increase time
  m_header.t += m_header.dt;
}

/** Computes half the kinetic part
  */
template <class T, int dim, int no_int_states>
void CRT_Base<T,dim,no_int_states>::Do_FT_Step_half()
{
  //Fourier transform
  for ( int c=0; c<no_int_states; c++ )
    m_fields[c]->ft(-1);
  double tmp1;

  for ( int i=0; i<no_int_states; i++ )
  {
    fftw_complex *Psi = m_fields[i]->Getp2In();

    #pragma omp parallel for private(tmp1)
    for ( int l=0; l<m_no_of_pts; l++ )
    {
      tmp1 = Psi[l][0];
      Psi[l][0] = Psi[l][0]*m_half_step[l][0] - Psi[l][1]*m_half_step[l][1];
      Psi[l][1] = Psi[l][1]*m_half_step[l][0] + tmp1*m_half_step[l][1];
    }
  }

  //Fourier transform back into real space
  for ( int c=0; c<no_int_states; c++ )
    m_fields[c]->ft(1);
  //Increase time
  m_header.t += 0.5*m_header.dt;
}

/** Solves the NL step including an external potential, if initialized
  */
template <class T, int dim, int no_int_states>
void CRT_Base<T,dim,no_int_states>::Do_NL_Step()
{
  const double dt = -m_header.dt;

  #pragma omp parallel
  {
    double re1, im1, tmp1, phi[no_int_states];

    vector<fftw_complex *> Psi;
    for ( int i=0; i<no_int_states; i++ )
      Psi.push_back(m_fields[i]->Getp2In());

    #pragma omp for
    for ( int l=0; l<m_no_of_pts; l++ )
    {
      for ( int i=0; i<no_int_states; i++ )
      {
        if ( m_potenial_initialized )
        {
          phi[i] = m_Potential[i][l];
        }
        else
        {
          phi[i] = 0;
        }
    	double tmp_density = Psi[i][l][0]*Psi[i][l][0] + Psi[i][l][1]*Psi[i][l][1];
    	if (tmp_density <= 0.0)
    	{
    		phi[i] += 0.0;
    	} else
    	{
    		//phi[i] += -this->m_b*log( tmp_density );
      	//phi[i] += this->m_gs[no_int_states*i]*tmp_density;
    	}
        phi[i] *= dt;
      }

      //exp(V)*Psi
      for ( int i=0; i<no_int_states; i++ )
      {
        sincos( phi[i], &im1, &re1 );

        tmp1 = Psi[i][l][0];
        Psi[i][l][0] = Psi[i][l][0]*re1 - Psi[i][l][1]*im1;
        Psi[i][l][1] = Psi[i][l][1]*re1 + tmp1*im1;
      }
    }
  }
}

/** Add a constant momentum to an internal state.
  *
  * @param px Momentum to be added
  * @param comp Number of the internal state to which the momentum shall be added
  */
template <class T, int dim, int no_int_states>
void CRT_Base<T,dim,no_int_states>::Setup_Momentum( CPoint<dim> px, const int comp )
{
  if ( comp<0 || comp>no_int_states ) throw std::string("Error in " + std::string(__func__) + ": comp out of bounds\n");

  #pragma omp parallel
  {
    CPoint<dim> x;
    double re, im, re2, im2;

    fftw_complex *Psi = m_fields[comp]->Getp2In();

    #pragma omp for
    for ( int l=0; l<m_no_of_pts; l++ )
    {
      x = this->m_fields[comp]->Get_x(l);
      //exp(p*x)
      sincos(px*x,&im,&re);

      re2 = Psi[l][0];
      im2 = Psi[l][1];
      Psi[l][0] = re2*re-im2*im;
      Psi[l][1] = re2*im+im2*re;
    }
  }
}

/** Calculate the expectation value of the postion of an internal state
  *
  * @param retval Reference to a CPoint object in which the expectation value will be saved
  * @param comp Compute the expectation value of the internal state comp
  */
template <class T, int dim, int no_int_states>
void CRT_Base<T,dim,no_int_states>::Expval_Position( CPoint<dim> &retval, const int comp )
{
  if ( comp<0 || comp>no_int_states ) throw std::string("Error in " + std::string(__func__) + ": comp out of bounds\n");

  double res[dim] = {};
  double *tmp;
  #pragma omp parallel
  {
    const int nthreads = omp_get_num_threads();
    const int ithread = omp_get_thread_num();

    double den;
    CPoint<dim> x;
    fftw_complex *Psi = m_fields[comp]->Getp2In();

    #pragma omp single
    {
      tmp = new double[dim*nthreads];
      for (int i=0; i<(dim*nthreads); i++) tmp[i] = 0;
    }

    #pragma omp for
    for ( int l=0; l<m_no_of_pts; l++ )
    {
      x = m_fields[comp]->Get_x(l);
      den = (Psi[l][0]*Psi[l][0]+Psi[l][1]*Psi[l][1]);
      for (int i=0; i<dim; i++ )
        tmp[ithread*dim+i] += x[i]*den;
    }

    #pragma omp for
    for (int i=0; i<dim; i++)
    {
      for (int t=0; t<nthreads; t++)
      {
        res[i] += tmp[dim*t + i];
      }
    }
  }

  delete [] tmp;

  for (int i=0; i<dim; i++ )
    retval[i] = m_ar*res[i];
}

/** Calculate the expectation value of the momentum of an internal state
  *
  * @param retval Reference to a CPoint object in which the expectation value will be saved
  * @param comp Compute the expectation value of the internal state comp
  */
template <class T, int dim, int no_int_states>
void CRT_Base<T,dim,no_int_states>::Expval_Momentum( CPoint<dim> &retval, const int comp )
{
  if ( comp<0 || comp>no_int_states ) throw std::string("Error in " + std::string(__func__) + ": comp out of bounds\n");

  double res[dim] = {};
  double *tmp;
  m_fields[comp]->ft(-1);
  #pragma omp parallel
  {
    const int nthreads = omp_get_num_threads();
    const int ithread = omp_get_thread_num();

    double den;
    CPoint<dim> k;
    fftw_complex *Psi = m_fields[comp]->Getp2In();

    #pragma omp single
    {
      tmp = new double[dim*nthreads];
      for (int i=0; i<(dim*nthreads); i++) tmp[i] = 0;
    }

    #pragma omp for
    for ( int l=0; l<m_no_of_pts; l++ )
    {
      k = m_fields[comp]->Get_k(l);
      den = (Psi[l][0]*Psi[l][0]+Psi[l][1]*Psi[l][1]);
      for (int i=0; i<dim; i++ )
        tmp[ithread*dim+i] += k[i]*den;
    }

    #pragma omp for
    for (int i=0; i<dim; i++)
    {
      for (int t=0; t<nthreads; t++)
      {
        res[i] += tmp[dim*t + i];
      }
    }
  }

  delete [] tmp;

  m_fields[comp]->ft(1);

  for (int i=0; i<dim; i++ )
    retval[i] = m_ar_k*res[i];
}

/** Compute the number of particles of an internal state
  *
  * @param comp Compute particle number of internal state comp
  */
template <class T, int dim, int no_int_states>
double CRT_Base<T,dim,no_int_states>::Get_Particle_Number( const int comp )
{
  if ( comp<0 || comp>no_int_states ) throw std::string("Error in " + std::string(__func__) + ": comp out of bounds\n");

  fftw_complex *Psi=m_fields[comp]->Getp2In();
  double retval=0.0;
  #pragma omp parallel for reduction(+:retval)
  for ( int l=0; l<m_no_of_pts; l++ )
  {
    retval += (Psi[l][0]*Psi[l][0] + Psi[l][1]*Psi[l][1]);
  }
  return m_ar*retval;
}

/** Write an internal state to a binary file
  *
  * @param filename
  * @param comp Write internal state comp
  */
template <class T, int dim, int no_int_states>
void CRT_Base<T,dim,no_int_states>::Save_Phi( std::string filename, const int comp )
{
  if ( comp<0 || comp>no_int_states ) throw std::string("Error in " + std::string(__func__) + ": comp out of bounds\n");

  char *header = reinterpret_cast<char *>(&m_header);
  char *Psi = reinterpret_cast<char *>(m_fields[comp]->Getp2In());

  ofstream file1( filename, ofstream::binary );
  file1.write( header, sizeof(generic_header) );
  file1.write( Psi, m_no_of_pts*sizeof(fftw_complex) );
  file1.close();
}

/** Append an internal state to a binary file
  *
  * @param filename
  * @param comp Write internal state comp
  */
template <class T, int dim, int no_int_states>
void CRT_Base<T,dim,no_int_states>::Append_Phi( std::string filename, const int comp )
{
  if ( comp<0 || comp>no_int_states ) throw std::string("Error in " + std::string(__func__) + ": comp out of bounds\n");

  char *header = reinterpret_cast<char *>(&m_header);
  char *Psi = reinterpret_cast<char *>(m_fields[comp]->Getp2In());

  ofstream file1( filename, ofstream::binary | ofstream::app );
  if (file1.fail()) {
    cout << "File " << filename << " could not be opened. Failbit: " << file1.rdstate() << endl;
    exit(EXIT_FAILURE);
  }
  file1.write( header, sizeof(generic_header) );
  file1.write( Psi, m_no_of_pts*sizeof(fftw_complex) );
  file1.close();
}

/** Write an array of doubles to a binary file
  *
  * @param data Write content of data to file. Number of elements of data must be the same as m_no_of_pts
  * @param filename
  */
template <class T, int dim, int no_int_states>
void CRT_Base<T,dim,no_int_states>::Save( double *data, std::string filename )
{
  generic_header header2 = m_header;
  header2.bComplex=false;

  char *header = reinterpret_cast<char *>(&header2);
  char *Psi = reinterpret_cast<char *>(data);

  ofstream file1( filename, ofstream::binary );
  file1.write( header, sizeof(generic_header) );
  file1.write( Psi, m_no_of_pts*sizeof(fftw_complex) );
  file1.close();
}

/** Write an array of complex numbers to a binary file
  *
  * @param data Write content of data to file. Number of elements of data must be the same as m_no_of_pts
  * @param filename
  */
template <class T, int dim, int no_int_states>
void CRT_Base<T,dim,no_int_states>::Save( fftw_complex *data, std::string filename )
{
  char *header = reinterpret_cast<char *>(&m_header);
  char *Psi = reinterpret_cast<char *>(data);

  ofstream file1( filename, ofstream::binary );
  file1.write( header, sizeof(generic_header) );
  file1.write( Psi, m_no_of_pts*sizeof(fftw_complex) );
  file1.close();
}

/** Run all the sequences defined in the xml file
  *
  * For further information about the sequences see sequence_item
  */
template <class T, int dim, int no_int_states>
void CRT_Base<T,dim,no_int_states>::run_sequence()
{
  if ( m_fields.size() != no_int_states )
  {
    std::cerr << "Critical Error: m_fields.size() != no_int_states\n";
    exit(EXIT_FAILURE);
  }

  StepFunction step_fct=nullptr;
  StepFunction half_step_fct=nullptr;
  StepFunction full_step_fct=nullptr;
  char filename[1024];

  std::cout << "FYI: Found " << m_params->m_sequence.size() << " sequences." << std::endl;

  try
  {
    half_step_fct = this->m_map_stepfcts.at("half_step");
    full_step_fct = this->m_map_stepfcts.at("full_step");
  }
  catch (const std::out_of_range &oor)
  {
    std::cerr << "Critical Error: Invalid fct ptr to ft_half_step or ft_full_step ()" << oor.what() << ")\n";
    exit(EXIT_FAILURE);
  }

  int seq_counter=1;

  //Loop through all sequences
  for ( auto seq : m_params->m_sequence )
  {
    if ( run_custom_sequence(seq) ) continue;

    if ( seq.name == "set_momentum" ) //Call Setup_Momentum
    {
      std::vector<std::string> vec;
      strtk::parse(seq.content,",",vec);
      CPoint<dim> P;

      assert( vec.size() > dim );
      assert( seq.comp <= dim );

      for ( int i=0; i<dim; i++ )
        P[i] = stod(vec[i]);

      this->Setup_Momentum( P, seq.comp );

      std::cout << "FYI: started new sequence " << seq.name << "\n";
      std::cout << "FYI: momentum set for component " << seq.comp << "\n";
      continue;
    }

    double max_duration = 0;
    for ( unsigned i = 0; i < seq.duration.size(); i++)
      if (seq.duration[i] > max_duration )
        max_duration = seq.duration[i];

    int subN = int(max_duration / seq.dt);
    int Nk = seq.Nk;
    int Na = subN / seq.Nk;

    std::cout << "FYI: started new sequence " << seq.name << "\n";
    std::cout << "FYI: sequence no : " << seq_counter << "\n";
    std::cout << "FYI: duration    : " << max_duration << "\n";
    std::cout << "FYI: dt          : " << seq.dt << "\n";
    std::cout << "FYI: Na          : " << Na << "\n";
    std::cout << "FYI: Nk          : " << Nk << "\n";
    std::cout << "FYI: Na*Nk*dt    : " << double(Na*Nk)*seq.dt << "\n";

    if ( double(Na*Nk)*seq.dt != max_duration )
      std::cout << "FYI: double(Na*Nk)*seq.dt != max_duration\n";

    if ( this->Get_dt() != seq.dt )
      this->Set_dt(seq.dt);

    try
    {
      step_fct = this->m_map_stepfcts.at(seq.name);
    }
    catch (const std::out_of_range &oor)
    {
      std::cerr << "Critical Error: Invalid squence name " << seq.name << "\n(" << oor.what() << ")\n";
      exit(EXIT_FAILURE);
    }

    for ( int k=0; k<no_int_states; k++ ) // Delete old packed Sequence
    {
      sprintf( filename, "Seq_%d_%d.bin", seq_counter, k+1 );
      std::remove(filename);
    }

    for ( int i=1; i<=Na; i++ )
    {
      (*half_step_fct)(this,seq);    // exp(T/2)
      for ( int j=2; j<=Nk; j++ )
      {
        (*step_fct)(this,seq);       // exp(V)
        (*full_step_fct)(this,seq);  // exp(T)
      }
      (*step_fct)(this,seq);         // exp(V)
      (*half_step_fct)(this,seq);    // exp(T/2)

      std::cout << "t = " << to_string(m_header.t) << std::endl;

      if ( seq.output_freq == freq::each )
      {
        for ( int k=0; k<no_int_states; k++ )
        {
          sprintf( filename, "%.3f_%d.bin", this->Get_t(), k+1 );
          this->Save_Phi( filename, k );
        }
      }

      if ( seq.output_freq == freq::packed )
      {
        for ( int k=0; k<no_int_states; k++ )
        {
          sprintf( filename, "Seq_%d_%d.bin", seq_counter, k+1 );
          this->Append_Phi( filename, k );
        }
      }

      if ( seq.compute_pn_freq == freq::each )
      {
        for ( int c=0; c<no_int_states; c++ )
          std::cout << "N[" << c << "] = " << this->Get_Particle_Number(c) << std::endl;
      }

      if ( seq.custom_freq == freq::each && m_custom_fct != nullptr )
      {
        (*m_custom_fct)(this,seq);
      }
    }

    if ( seq.output_freq == freq::last )
    {
      for ( int k=0; k<no_int_states; k++ )
      {
        sprintf( filename, "%.3f_%d.bin", this->Get_t(), k+1 );
        this->Save_Phi( filename, k );
      }
    }

    if ( seq.compute_pn_freq == freq::last )
    {
      for ( int c=0; c<no_int_states; c++ )
        std::cout << "N[" << c << "] = " << this->Get_Particle_Number(c) << std::endl;
    }

    if ( seq.custom_freq == freq::last && m_custom_fct != nullptr )
    {
      (*m_custom_fct)(this,seq);
    }

    seq_counter++;
  } // end of sequence loop
}

/// Defines Output for << operator for CRT_Base objects
template <class T, int dim, int no_int_states>
void CRT_Base<T,dim,no_int_states>::Dump_2( ofstream &stream )
{
  stream << m_header.t << "\t";
  //stream << Get_N() << "\t";
}

/** \relates CRT_Base
  * Overload << for CRT_Base objects. Output ist defined in Dump_2
  */
template<class T, int dim, int no_int_states>
ofstream &operator<<( ofstream &stream, CRT_Base<T,dim,no_int_states> &obj )
{
  obj.Dump_2(stream);
  return stream;
}

#endif
