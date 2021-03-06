#ifndef Matrix_Operations_hh
#define Matrix_Operations_hh

#include <vector>

using std::vector;

class Matrix_Operations
{
private:

public:

    Matrix_Operations();
    
    void dot(vector<double> const &a_data,
             vector<double> const &b_data,
             double &x_data,
             unsigned number_of_elements = 3);

    void cross(vector<double> const &a_data,
               vector<double> const &b_data,
               vector<double> &x_data,
               unsigned number_of_elements = 3);

    void multiply(vector<double> const &a_data,
                  vector<double> const &b_data,
                  vector<double> &x_data,
                  unsigned n,
                  unsigned m,
                  unsigned p);

    void solve(vector<double> const &a,
               vector<double> const &b,
               vector<double> &x,
               unsigned number_of_elements);
};

#endif
