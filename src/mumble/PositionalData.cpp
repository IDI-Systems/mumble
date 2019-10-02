// Copyright 2005-2019 The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "PositionalData.h"
#include <cmath>
#include <cstdlib>
#include <QtCore/QReadLocker>

Vector3D::Vector3D() : x(0.0f), y(0.0f), z(0.0f) {
}

Vector3D::Vector3D(float x, float y, float z) : x(x), y(y), z(z) {
}

Vector3D::Vector3D(const Vector3D& other) : x(other.x), y(other.y), z(other.z) {
}

Vector3D::~Vector3D() {
}

float Vector3D::operator[](Coord coord) const {
	switch(coord) {
		case Coord::X:
			return this->x;
		case Coord::Y:
			return this->y;
		case Coord::Z:
			return this->z;
		default:
			// invalid index
			throw std::out_of_range("May only access x, y or z");
	}
}

Vector3D Vector3D::operator*(float factor) const {
	return { this->x * factor, this->y * factor, this->z * factor };
}

Vector3D Vector3D::operator/(float divisor) const {
	return { this->x / divisor, this->y / divisor, this->z / divisor };
}

void Vector3D::operator*=(float factor) {
	this->x *= factor;
	this->y *= factor;
	this->z *= factor;
}

void Vector3D::operator/=(float divisor) {
	this->x /= divisor;
	this->y /= divisor;
	this->z /= divisor;
}

bool Vector3D::operator==(const Vector3D& other) const {
	return this->equals(other, 0.0f);
}

Vector3D Vector3D::operator-(const Vector3D& other) const {
	return { this->x - other.x, this->y - other.y, this->z - other.z };
}

Vector3D Vector3D::operator+(const Vector3D& other) const {
	return { this->x + other.x, this->y + other.y, this->z + other.z };
}

float Vector3D::normSquared() const {
	return this->x * this->x + this->y * this->y + this->z * this->z;
}

float Vector3D::norm() const {
	return sqrt(this->normSquared());
}

float Vector3D::dotProduct(const Vector3D& other) const {
	return this->x * other.x + this->y + other.y + this->z + other.z;
}

Vector3D Vector3D::crossProduct(const Vector3D& other) const {
	return { this->y * other.z - this->z * other.y, this->z * other.x - this->x * other.z, this->x * other.y - this->y * other.x };
}

bool Vector3D::equals(const Vector3D& other, float threshold) const {
	if (threshold == 0.0f) {
		return this->x == other.x && this->y == other.y && this->z == other.z;
	} else {
		threshold = abs(threshold);

		return abs(this->x - other.x) < threshold && abs(this->y - other.y) < threshold && abs(this->z - other.z) < threshold;
	}
}

bool Vector3D::isZero(float threshold) const {
	if (threshold == 0.0f) {
		return this->x == 0.0f && this->y == 0.0f && this->z == 0.0f;
	} else {
		return abs(this->x) < threshold && abs(this->y) < threshold && abs(this->z) < threshold;
	}
}

void Vector3D::normalize() {
	float len = this->norm();

	this->x /= len;
	this->y /= len;
	this->z /= len;
}

void Vector3D::toZero() {
	this->x = 0.0f;
	this->y = 0.0f;
	this->z = 0.0f;
}

PositionalData::PositionalData() : playerPos(), playerDir(), playerAxis(), cameraPos(), cameraDir(), cameraAxis(),
	context(), identity(), lock(QReadWriteLock::Recursive) {
}

PositionalData::PositionalData(Position3D playerPos, Vector3D playerDir, Vector3D playerAxis, Position3D cameraPos,
	Vector3D cameraDir, Vector3D cameraAxis, QString context, QString identity) : playerPos(playerPos), playerDir(playerDir),
		playerAxis(playerAxis), cameraPos(cameraPos), cameraDir(cameraDir), cameraAxis(cameraAxis), context(context), identity(identity),
			lock(QReadWriteLock::Recursive) {
}

PositionalData::~PositionalData() {
}


void PositionalData::getPlayerPos(Position3D& pos) const {
	QReadLocker lock(&this->lock);

	pos = this->playerPos;
}

Position3D PositionalData::getPlayerPos() const {
	QReadLocker lock(&this->lock);

	return this->playerPos;
}

void PositionalData::getPlayerDir(Vector3D& vec) const {
	QReadLocker lock(&this->lock);

	vec = this->playerDir;
}

Vector3D PositionalData::getPlayerDir() const {
	QReadLocker lock(&this->lock);

	return this->playerDir;
}

void PositionalData::getPlayerAxis(Vector3D& vec) const {
	QReadLocker lock(&this->lock);

	vec = this->playerAxis;
}

Vector3D PositionalData::getPlayerAxis() const {
	QReadLocker lock(&this->lock);

	return this->playerAxis;
}

void PositionalData::getCameraPos(Position3D& pos) const {
	QReadLocker lock(&this->lock);

	pos = this->cameraPos;
}

Position3D PositionalData::getCameraPos() const {
	QReadLocker lock(&this->lock);

	return this->cameraPos;
}

void PositionalData::getCameraDir(Vector3D& vec) const {
	QReadLocker lock(&this->lock);

	vec = this->cameraDir;
}

Vector3D PositionalData::getCameraDir() const {
	QReadLocker lock(&this->lock);

	return this->cameraDir;
}

void PositionalData::getCameraAxis(Vector3D& vec) const {
	QReadLocker lock(&this->lock);

	vec = this->cameraAxis;
}

Vector3D PositionalData::getCameraAxis() const {
	QReadLocker lock(&this->lock);

	return this->cameraAxis;
}

QString PositionalData::getPlayerIdentity() const {
	QReadLocker lock(&this->lock);

	return this->identity;
}

QString PositionalData::getContext() const {
	QReadLocker lock(&this->lock);

	return this->context;
}

void PositionalData::reset() {
	this->playerPos.toZero();
	this->playerDir.toZero();
	this->playerAxis.toZero();
	this->cameraPos.toZero();
	this->cameraDir.toZero();
	this->cameraAxis.toZero();
	this->context = QString();
	this->identity = QString();
}
