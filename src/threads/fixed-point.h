#ifndef THREADS_FIXED_POINT_H
#define THREADS_FIXES_POINT_H

#define Q 14

static int F = 1<<Q ;

/*
Fixed-point numbers are in P.Q format
X and Y are always fixed-point numbers
N is an integer
*/

// Input: N is an integer
// Output: Fixed-point number
static int convert_to_fixed_point ( int n )
{
	return n*F ;
}

// Function to convert to int with ROUNDING TO NEAREST INTEGER
// Input: X is a fixed-point number
// Output: Integer rounded to nearest
static int convert_to_int_round_near ( int x )
{
	if ( x >= 0 )
	{
		return (x+F/2)/F ;
	}
	else
	{
		return (x-F/2)/F ;
	}
}

// Input: X and Y are fixed-point numbers
// Output: Fixed-point number
static int fixed_point_add ( int x, int y )
{
	return x+y ;
}

// Input: X and Y are fixed-point numbers
// Output: Fixed-point number
static int fixed_point_sub ( int x, int y )
{
	return x-y ;
}

// Input: X and Y are fixed-point numbers
// Output: Fixed-point number
static int fixed_point_mul ( int x, int y )
{
	return ((int64_t) x) * y / F ;
}

// Input: X is fixed-point and N is int
// Output: Fixed-point number
static int fixed_point_mul_with_int ( int x, int n )
{
	return x*n ;
}

// Input: X and Y are fixed-point numbers
// Output: Fixed-point number
static int fixed_point_div ( int x, int y )
{
	return ((int64_t) x) * F / y ;
}

#endif
