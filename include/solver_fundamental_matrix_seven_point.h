#pragma once

#include "solver_engine.h"
#include "fundamental_estimator.h"

namespace solver
{
	// This is the estimator class for estimating a homography matrix between two images. A model estimation method and error calculation method are implemented
	class FundamentalMatrixSevenPointSolver : public SolverEngine
	{
	public:
		FundamentalMatrixSevenPointSolver()
		{
		}

		~FundamentalMatrixSevenPointSolver()
		{
		}

		static constexpr size_t sampleSize()
		{
			return 7;
		}

		inline bool estimateModel(
			const cv::Mat& data_,
			const size_t *sample_,
			size_t sample_number_,
			std::vector<Model> &models_) const;
	};

	inline bool FundamentalMatrixSevenPointSolver::estimateModel(
		const cv::Mat& data_,
		const size_t *sample_,
		size_t sample_number_,
		std::vector<Model> &models_) const
	{
		Eigen::MatrixXd coefficients(sample_number_, 9);
		const double *data_ptr = reinterpret_cast<double *>(data_.data);
		const int cols = data_.cols;
		double c[4];
		double t0, t1, t2;
		int i, n;

		// Form a linear system: i-th row of A(=a) represents
		// the equation: (m2[i], 1)'*F*(m1[i], 1) = 0
		for (i = 0; i < 7; i++)
		{
			const int sample_idx = sample_[i];
			const int offset = cols * sample_idx;

			double x0 = data_ptr[offset],
				y0 = data_ptr[offset + 1],
				x1 = data_ptr[offset + 2],
				y1 = data_ptr[offset + 3];

			coefficients(i, 0) = x1 * x0;
			coefficients(i, 1) = x1 * y0;
			coefficients(i, 2) = x1;
			coefficients(i, 3) = y1 * x0;
			coefficients(i, 4) = y1 * y0;
			coefficients(i, 5) = y1;
			coefficients(i, 6) = x0;
			coefficients(i, 7) = y0;
			coefficients(i, 8) = 1;
		}

		// A*(f11 f12 ... f33)' = 0 is singular (7 equations for 9 variables), so
		// the solution is linear subspace of dimensionality 2.
		// => use the last two singular std::vectors as a basis of the space
		// (according to SVD properties)
		Eigen::JacobiSVD<Eigen::MatrixXd> svd(
			// Theoretically, it would be faster to apply SVD only to matrix coefficients, but
			// multiplication is faster than SVD in the Eigen library. Therefore, it is faster
			// to apply SVD to a smaller matrix.
			coefficients.transpose() * coefficients,
			Eigen::ComputeFullV);

		Eigen::Matrix<double, 9, 1> f1 =
			svd.matrixV().block<9, 1>(0, 7);
		Eigen::Matrix<double, 9, 1> f2 =
			svd.matrixV().block<9, 1>(0, 8);

		// f1, f2 is a basis => lambda*f1 + mu*f2 is an arbitrary f. matrix.
		// as it is determined up to a scale, normalize lambda & mu (lambda + mu = 1),
		// so f ~ lambda*f1 + (1 - lambda)*f2.
		// use the additional constraint det(f) = det(lambda*f1 + (1-lambda)*f2) to find lambda.
		// it will be a cubic equation.
		// find c - polynomial coefficients.
		for (i = 0; i < 9; i++)
			f1[i] -= f2[i];

		t0 = f2[4] * f2[8] - f2[5] * f2[7];
		t1 = f2[3] * f2[8] - f2[5] * f2[6];
		t2 = f2[3] * f2[7] - f2[4] * f2[6];

		c[0] = f2[0] * t0 - f2[1] * t1 + f2[2] * t2;

		c[1] = f1[0] * t0 - f1[1] * t1 + f1[2] * t2 -
			f1[3] * (f2[1] * f2[8] - f2[2] * f2[7]) +
			f1[4] * (f2[0] * f2[8] - f2[2] * f2[6]) -
			f1[5] * (f2[0] * f2[7] - f2[1] * f2[6]) +
			f1[6] * (f2[1] * f2[5] - f2[2] * f2[4]) -
			f1[7] * (f2[0] * f2[5] - f2[2] * f2[3]) +
			f1[8] * (f2[0] * f2[4] - f2[1] * f2[3]);

		t0 = f1[4] * f1[8] - f1[5] * f1[7];
		t1 = f1[3] * f1[8] - f1[5] * f1[6];
		t2 = f1[3] * f1[7] - f1[4] * f1[6];

		c[2] = f2[0] * t0 - f2[1] * t1 + f2[2] * t2 -
			f2[3] * (f1[1] * f1[8] - f1[2] * f1[7]) +
			f2[4] * (f1[0] * f1[8] - f1[2] * f1[6]) -
			f2[5] * (f1[0] * f1[7] - f1[1] * f1[6]) +
			f2[6] * (f1[1] * f1[5] - f1[2] * f1[4]) -
			f2[7] * (f1[0] * f1[5] - f1[2] * f1[3]) +
			f2[8] * (f1[0] * f1[4] - f1[1] * f1[3]);

		c[3] = f1[0] * t0 - f1[1] * t1 + f1[2] * t2;

		// solve the cubic equation; there can be 1 to 3 roots ...
		Eigen::Matrix<double, 4, 1> polynomial;
		for (auto i = 0; i < 4; ++i)
			polynomial(i) = c[i];
		Eigen::PolynomialSolver<double, 3> psolve(polynomial);

		std::vector<double> real_roots;
		psolve.realRoots(real_roots);

		n = real_roots.size();
		if (n < 1 || n > 3)
			return false;

		double f[8];
		for (const double &root : real_roots)
		{
			// for each root form the fundamental matrix
			double lambda = root, mu = 1.;
			double s = f1[8] * root + f2[8];

			// normalize each matrix, so that F(3,3) (~fmatrix[8]) == 1
			if (fabs(s) > std::numeric_limits<double>::epsilon())
			{
				mu = 1.0f / s;
				lambda *= mu;

				for (auto i = 0; i < 8; ++i)
					f[i] = f1[i] * lambda + f2[i] * mu;

				FundamentalMatrix model;
				model.descriptor << f[0], f[1], f[2],
					f[3], f[4], f[5],
					f[6], f[7], 1.0;
				models_.push_back(model);
			}
		}

		return true;
	}
}