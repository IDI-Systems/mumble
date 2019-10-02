// Copyright 2005-2019 The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_POSITIONAL_AUDIO_CONTEXT_H_
#define MUMBLE_MUMBLE_POSITIONAL_AUDIO_CONTEXT_H_

#include <QtCore/QString>
#include <QtCore/QReadWriteLock>

enum class Coord {X=0,Y,Z};

class Vector3D {
	public:
		float x;
		float y;
		float z;

		float operator[](Coord coord) const;
		Vector3D operator*(float factor) const;
		Vector3D operator/(float divisor) const;
		void operator*=(float factor);
		void operator/=(float divisor);
		bool operator==(const Vector3D& other) const;
		Vector3D operator-(const Vector3D& other) const;
		Vector3D operator+(const Vector3D& other) const;
		Vector3D& operator=(const Vector3D& other) = default;

		// allow explicit conversions from this struct to a float-array / float-pointer
		explicit operator const float*() const { return &x; };
		explicit operator float*() { return &x; };

		Vector3D();
		Vector3D(float x, float y, float z);
		Vector3D(const Vector3D& other);
		~Vector3D();
		float normSquared() const;
		float norm() const;
		float dotProduct(const Vector3D& other) const;
		Vector3D crossProduct(const Vector3D& other) const;
		bool equals(const Vector3D& other, float threshold = 0.0f) const;
		bool isZero(float threshold = 0.0f) const;
		void normalize();
		void toZero();
};

// As we're casting the vector struct to float-arrays, we have to make sure that the compiler won't introduce any padding
// into the structure
static_assert(sizeof(Vector3D) == 3*sizeof(float), "The compiler added padding to the Vector3D structure so it can't be cast to a float-array!");

// create an alias for Vector3D as it can also represent a position
typedef Vector3D Position3D;

class PositionalData {
	protected:
		Position3D playerPos;
		Vector3D playerDir;
		Vector3D playerAxis;
		Position3D cameraPos;
		Vector3D cameraDir;
		Vector3D cameraAxis;
		QString context;
		QString identity;
		mutable QReadWriteLock lock;

	public:
		PositionalData();
		PositionalData(Position3D playerPos, Vector3D playerDir, Vector3D playerAxis, Position3D cameraPos, Vector3D cameraDir,
				Vector3D cameraAxis, QString context, QString identity);
		~PositionalData();
		void getPlayerPos(Position3D& pos) const;
		Position3D getPlayerPos() const;
		void getPlayerDir(Vector3D& vec) const;
		Vector3D getPlayerDir() const;
		void getPlayerAxis(Vector3D& vec) const;
		Vector3D getPlayerAxis() const;
		void getCameraPos(Position3D& pos) const;
		Position3D getCameraPos() const;
		void getCameraDir(Vector3D& vec) const;
		Vector3D getCameraDir() const;
		void getCameraAxis(Vector3D& vec) const;
		Vector3D getCameraAxis() const;
		QString getPlayerIdentity() const;
		QString getContext() const;
		void reset();
};

#endif
