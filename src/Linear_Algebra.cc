#include "Linear_Algebra.hh"

#include <iostream>
#include <vector>

#include <boost/numeric/ublas/matrix.hpp>
#include <boost/numeric/ublas/vector.hpp>
#include <boost/numeric/ublas/lu.hpp>
#include <boost/numeric/ublas/io.hpp>

#include <Epetra_SerialDenseMatrix.h>
#include <Epetra_SerialDenseVector.h>
#include <Epetra_SerialDenseSolver.h>

#include <Amesos.h>
#include <AztecOO.h>
#include <AztecOO_Version.h>
#ifdef EPETRA_MPI
#  include <mpi.h>
#  include <Epetra_MpiComm.h>
#else
#  include <Epetra_SerialComm.h>
#endif
#include <Epetra_Map.h>
#include <Epetra_CrsMatrix.h>
#include <Epetra_Vector.h>
#include <Epetra_LinearProblem.h>

#include <gsl/gsl_blas.h>
#include <gsl/gsl_linalg.h>
#include <gsl/gsl_matrix.h>
#include <gsl/gsl_vector.h>

#include "Check.hh"

using namespace std;

Linear_Algebra::
Linear_Algebra(unsigned size):
    size_(size)
{
}

// linear system of equations

// solves linear system Ax=b
void Linear_Algebra::
solve(vector<double> &a_data,
      vector<double> &b_data,
      vector<double> &x_data)
{
    Check(a_data.size() == size()*size(), "A size");
    Check(b_data.size() == size(), "b size");
    Check(x_data.size() == size(), "x size");

    // default solve
    if (size() < 300)
    {
        gsl_solve(a_data,
                  b_data,
                  x_data);
    }
    else
    {
        epetra_solve(a_data,
                     b_data,
                     x_data);
    }
}

// works well: fastest for small problems
void Linear_Algebra::
gsl_solve(vector<double> &a_data,
          vector<double> &b_data,
          vector<double> &x_data)
{
    Check(a_data.size() == size()*size(), "A size");
    Check(b_data.size() == size(), "b size");
    Check(x_data.size() == size(), "x size");

    // assign data
    gsl_matrix_view a = gsl_matrix_view_array(&a_data[0],
                                              size(),
                                              size());
    gsl_vector_view b = gsl_vector_view_array(&b_data[0],
                                              size());
    gsl_vector_view x = gsl_vector_view_array(&x_data[0],
                                              size());

    // solve
    int s;
    gsl_permutation *p = gsl_permutation_alloc(size());
    gsl_linalg_LU_decomp(&a.matrix, p, &s);
    gsl_linalg_LU_solve(&a.matrix, p, &b.vector, &x.vector);
    gsl_permutation_free(p);
}

// not working: returns a zero vector
void Linear_Algebra::
boost_solve(vector<double> &a_data,
            vector<double> &b_data,
            vector<double> &x_data)
{
    // Check(false, "boost solver not working");

    namespace ublas = boost::numeric::ublas;

    Check(a_data.size() == size()*size(), "A size");
    Check(b_data.size() == size(), "b size");
    Check(x_data.size() == size(), "x size");

    // assign data
    ublas::matrix<double> a(size(), size());
    ublas::vector<double> b(size());
    ublas::vector<double> x(size());

    for (unsigned i=0; i<size(); ++i)
    {
        b(i) = b_data[i];

        for (unsigned j=0; j<size(); ++j)
        {
            a(j,i) = a_data[j + size()*i];
        }
    }
    
    ublas::permutation_matrix<double> p(size());
    ublas::lu_factorize(a, p);
    x = b;
    ublas::lu_substitute(a,p,x);
}

// working well: fastest for large problems
void Linear_Algebra::
epetra_solve(vector<double> &a_data,
             vector<double> &b_data,
             vector<double> &x_data)
{
    Check(a_data.size() == size()*size(), "A size");
    Check(b_data.size() == size(), "b size");
    Check(x_data.size() == size(), "x size");

    Epetra_SerialDenseMatrix a(View, &a_data[0], size(), size(), size());
    Epetra_SerialDenseVector x(size());
    Epetra_SerialDenseVector b(View, &b_data[0], size());

    // cout << a << endl;
    // cout << b << endl;
    // cout << x << endl;

    Epetra_SerialDenseSolver solver;

    solver.SetMatrix(a);
    solver.SetVectors(x, b);
    solver.SolveWithTranspose(true);
    if (solver.ShouldEquilibrate())
    {
        solver.FactorWithEquilibration(true);
    }
    // solver.SolveToRefinedSolution(true);
    solver.Factor();
    solver.Solve();
    // solver.ApplyRefinement();
    solver.UnequilibrateLHS();
    
    if (!solver.Solved())// || !solver.SolutionRefined())
    {
        Check(false, "epetra failed to solve");
    }

    x_data.assign(solver.X(), solver.X() + size());
}

// works well, but slow: meant for sparse matrices
void Linear_Algebra::
amesos_solve(vector<double> &a_data,
             vector<double> &b_data,
             vector<double> &x_data)
{
    Check(a_data.size() == size()*size(), "A size");
    Check(b_data.size() == size(), "b size");
    Check(x_data.size() == size(), "x size");

    int const index_base = 0;
    int const num_elements = size();
    string solver_type = "Klu";
    
    vector<int> const num_entries_per_row(size(), size());
    vector<int> column_indices(size());
    for (unsigned i=0; i<size(); ++i)
    {
        column_indices[i] = i;
    }
    
#ifdef EPETRA_MPI
    Epetra_MpiComm comm(MPI_COMM_WORLD);
#else
    Epetra_SerialComm comm;
#endif
    Epetra_Map map(num_elements, index_base, comm);
    Epetra_Vector lhs(View, map, &x_data[0]);
    Epetra_Vector rhs(View, map, &b_data[0]);
    Epetra_CrsMatrix matrix(View, map, size());

    for (unsigned i=0; i<size(); ++i)
    {
        matrix.InsertGlobalValues(i, num_entries_per_row[i], &a_data[size()*i], &column_indices[0]);
    }
    matrix.FillComplete();
    
    Epetra_LinearProblem problem(&matrix, &lhs, &rhs);
    Amesos factory;
    
    Amesos_BaseSolver* solver = factory.Create(solver_type, problem);
    if (solver == NULL)
    {
        Check(false, "specified solver is not available");
    }
    
    solver->SymbolicFactorization();
    solver->NumericFactorization();
    solver->Solve();
}

// takes way too many iterations to converge: not meant for dense matrices
void Linear_Algebra::
aztec_solve(vector<double> &a_data,
            vector<double> &b_data,
            vector<double> &x_data)
{
    // Check(false, "aztec solver not guaranteed to converge");

    Check(a_data.size() == size()*size(), "A size");
    Check(b_data.size() == size(), "b size");
    Check(x_data.size() == size(), "x size");

    int const index_base = 0;
    int const num_elements = size();
    int const max_iterations = 10000;
    int const tolerance = 1.0E-6;
    
    vector<int> const num_entries_per_row(size(), size());
    vector<int> column_indices(size());
    for (unsigned i=0; i<size(); ++i)
    {
        column_indices[i] = i;
    }
    
#ifdef EPETRA_MPI
    Epetra_MpiComm comm( MPI_COMM_WORLD );
#else
    Epetra_SerialComm comm;
#endif
    Epetra_Map map(num_elements, index_base, comm);
    Epetra_Vector lhs(View, map, &x_data[0]);
    Epetra_Vector rhs(View, map, &b_data[0]);
    Epetra_CrsMatrix matrix(View, map, size());

    for (unsigned i=0; i<size(); ++i)
    {
        matrix.InsertGlobalValues(i, num_entries_per_row[i], &a_data[size()*i], &column_indices[0]);
    }
    matrix.FillComplete();
    
    Epetra_LinearProblem problem(&matrix, &lhs, &rhs);
    AztecOO solver(problem);
    solver.SetAztecOption(AZ_precond, AZ_Jacobi);
    solver.SetAztecOption(AZ_solver, AZ_gmres);
    solver.SetAztecOption(AZ_output, AZ_none);
    solver.Iterate(max_iterations, tolerance);
}

// simple vector operations

void Linear_Algebra::
dot(vector<double> const &a_data,
    vector<double> const &b_data,
    double &x_data)
{
    Check(a_data.size() == size(), "a size");
    Check(b_data.size() == size(), "b size");
    
    double sum = 0;
    for (unsigned i=0; i<size(); ++i)
    {
        sum += a_data[i]*b_data[i];
    }
    x_data = sum;
}

void Linear_Algebra::
cross(vector<double> const &a_data,
      vector<double> const &b_data,
      vector<double> &x_data)
{
    Check(size() == 3, "size must be 3 for cross product");
    Check(a_data.size() == size(), "a size");
    Check(b_data.size() == size(), "b size");
    Check(x_data.size() == size(), "x size");
    
    x_data[0] = a_data[1]*b_data[2] - a_data[2]*b_data[1];
    x_data[1] = a_data[2]*b_data[0] - a_data[0]*b_data[2];
    x_data[2] = a_data[0]*b_data[1] - a_data[1]*b_data[0];
}

// multiply matrices or vectors together

// computes x=a*b
void Linear_Algebra::
multiply(vector<double> const &a_data,
         vector<double> const &b_data,
         vector<double> &x_data)
{
    bool a_vec = a_data.size() == size();
    bool a_mat = a_data.size() == size()*size();
    bool b_vec = b_data.size() == size();
    bool b_mat = b_data.size() == size()*size();
    bool x_vec = x_data.size() == size();
    bool x_mat = x_data.size() == size()*size();

    Check((a_vec || a_mat), "A size");
    Check((b_vec || b_mat), "B size");
    
    if (a_vec && b_vec)
    {
        Check(x_vec, "x must be vector");

        multiply_vector_vector(a_data, 
                               b_data,
                               x_data);
        
        return;
    }
    else if (a_mat && b_vec)
    {
        Check(x_vec, "x must be vector");
        
        multiply_matrix_vector(a_data, 
                               b_data,
                               x_data);
        
        return;
    }
    else if (a_mat && b_mat)
    {
        Check(x_mat, "x must be matrix");
        
        multiply_matrix_matrix(a_data,
                               b_data,
                               x_data);
        
        return;
    }
    else
    {
        Check(a_vec && b_mat, "a must be matrix");
        Check(false, "failed to multiply");
        return;
    }
}

void Linear_Algebra::
multiply_vector_vector(vector<double> const &a_data,
                       vector<double> const &b_data,
                       vector<double> &x_data)
{
    Check(a_data.size() == size(), "a size");
    Check(b_data.size() == size(), "b size");
    Check(x_data.size() == size(), "x size");
    
    for (unsigned i=0; i<size(); ++i)
    {
        x_data[i] = a_data[i]*b_data[i];
    }
}

void Linear_Algebra::
multiply_matrix_vector(vector<double> const &a_data,
                       vector<double> const &b_data,
                       vector<double> &x_data)
{
    Check(a_data.size() == size()*size(), "a size");
    Check(b_data.size() == size(), "b size");
    Check(x_data.size() == size(), "x size");
    
    for (unsigned i=0; i<size(); ++i)
    {
        double sum = 0;
        for (unsigned k=0; k<size(); ++k)
        {
            unsigned index1 = k + size()*i;
                
            sum += a_data[index1]*b_data[k];
        }
        
        x_data[i] = sum;
    }
}

void Linear_Algebra::
multiply_matrix_matrix(vector<double> const &a_data,
                       vector<double> const &b_data,
                       vector<double> &x_data)
{
    Check(a_data.size() == size()*size(), "a size");
    Check(b_data.size() == size()*size(), "b size");
    Check(x_data.size() == size()*size(), "x size");
    
    for (unsigned i=0; i<size(); ++i)
    {
        for (unsigned j=0; j<size(); ++j)
        {
            double sum = 0;
            for (unsigned k=0; k<size(); ++k)
            {
                unsigned index1 = k + size()*i;
                unsigned index2 = j + size()*k;
                
                sum += a_data[index1]*b_data[index2];
            }
            
            unsigned index = j + size()*i;
            
            x_data[i + size()*j] = sum;
        }
    }
}