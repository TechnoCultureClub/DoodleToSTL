//
// Copyright (C) 2016 Emmanuel Durand
//
// This file is part of doodle2stl.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Splash is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this software.  If not, see <http://www.gnu.org/licenses/>.
//

#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdio.h>
#include <string>
#include <unistd.h>
#include <vector>

#include <opencv/cv.hpp>

using namespace std;

/*************/
struct Roi
{
    int x{0};
    int y{0};
    int w{0};
    int h{0};
};

/*************/
struct Vertex
{
    Vertex(){};
    Vertex(float _x, float _y, float _z) : x(_x), y(_y), z(_z) {}
    float x{0.f};
    float y{0.f};
    float z{0.f};
};

struct Face
{
    Face(){};
    vector<uint32_t> indices;
};

struct Mesh
{
    vector<Vertex> vertices{};
    vector<Face> faces{};
};

/*************/
// Filters the image to keep the doodle
void filterImage(cv::Mat& image)
{
    // Resize
    cv::Mat tmpMat;
    if (image.rows > image.cols)
        cv::resize(image, tmpMat, cv::Size((800 * image.cols) / image.rows, 800));
    else
        cv::resize(image, tmpMat, cv::Size(800, (800 * image.rows) / image.cols));
    image = tmpMat;

    // Edge detection
    cv::threshold(image, image, 64, 255, cv::THRESH_BINARY_INV);

    // TODO: hey, in most cases, this is enough
    return;

    // TODO: keeping the rest for further use, if ever
    /*
    cv::Mat edges(image.rows, image.cols, CV_8U);
    cv::Canny(image, edges, 64, 128, 3);

    // Edge filtering
    // auto structElem = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(7, 7));
    // cv::morphologyEx(edges, tmpMat, cv::MORPH_CLOSE, structElem, cv::Point(-1, -1), 1);
    // cv::morphologyEx(tmpMat, edges, cv::MORPH_OPEN, structElem, cv::Point(-1, -1), 2);
    // edges = tmpMat;

    // Find and draw the biggest contour
    vector<vector<cv::Point>> contours;
    vector<cv::Vec4i> hierarchy;
    cv::findContours(edges, contours, hierarchy, cv::RETR_CCOMP, cv::CHAIN_APPROX_SIMPLE);

    tmpMat = cv::Mat::zeros(image.rows, image.cols, CV_8U);
    for (int i = 0; i >= 0; i = hierarchy[i][0])
    {
        if (cv::contourArea(contours[i]) < 64)
            continue;

        cv::Scalar color(255);
        drawContours(tmpMat, contours, i, color, -1);

        for (int j = hierarchy[i][2]; j >= 0; j = hierarchy[j][0])
        {
            if (cv::contourArea(contours[j]) < 64)
                continue;

            cv::Scalar color(0, 0, 0);
            drawContours(tmpMat, contours, j, color, -1);
        }
    }

    image = tmpMat;
    */
}

/*************/
// Converts a binary image to a 2D mesh
// The conversion is based on an existing matrice of vertices, with a given resolution
bool getVertexAt(const cv::Mat& image, int x, int y, Vertex& vertex)
{
    int count = 0;
    float weight = 0.f;
    if (image.at<uint8_t>(y, x) > 16)
    {
        vertex.x += x * image.at<uint8_t>(y, x);
        vertex.y += y * image.at<uint8_t>(y, x);
        weight += image.at<uint8_t>(y, x);
        ++count;
    }
    if (y + 1 < image.rows && image.at<uint8_t>(y + 1, x) > 16)
    {
        vertex.x += x * image.at<uint8_t>(y + 1, x);
        vertex.y += (y + 1) * image.at<uint8_t>(y + 1, x);
        weight += image.at<uint8_t>(y + 1, x);
        ++count;
    }
    if (y + 1 < image.rows && x + 1 < image.cols && image.at<uint8_t>(y + 1, x + 1) > 16)
    {
        vertex.x += (x + 1) * image.at<uint8_t>(y + 1, x + 1);
        vertex.y += (y + 1) * image.at<uint8_t>(y + 1, x + 1);
        weight += image.at<uint8_t>(y + 1, x + 1);
        ++count;
    }
    if (x + 1 < image.cols && image.at<uint8_t>(y, x + 1) > 16)
    {
        vertex.x += (x + 1) * image.at<uint8_t>(y, x + 1);
        vertex.y += y * image.at<uint8_t>(y, x + 1);
        weight += image.at<uint8_t>(y, x + 1);
        ++count;
    }

    if (count >= 2)
    {
        vertex.x /= weight;
        vertex.y /= weight;
        return true;
    }
    else
    {
        return false;
    }
}

int findBottomVertex(const vector<Vertex>& line, float x)
{
    if (line.size() == 0)
        return -1;

    for (int i = 0; i < line.size(); ++i)
        if (abs(x - line[i].x) < 0.01)
            return i;

    return -1;
}

int binaryToMesh(const cv::Mat& image, Mesh& mesh, int resolution, float height)
{
    // Reduce image resolution
    cv::Mat map;
    cv::resize(image, map, cv::Size(resolution, (resolution * image.rows) / image.cols));

    // We consider a maximum size of 100x100mm, so we scale it if resolution is different from 100
    float ratio = 100.f / (float)resolution;

    // Initialize the mesh
    mesh.vertices.clear();
    vector<int> indices(map.cols * map.rows, -1);

    // First pass: add vertices where needed
    vector<vector<Vertex>> vertices;
    int vertexCount = 0;
    for (int y = 0; y < map.rows - 1; ++y)
    {
        vertices.push_back(vector<Vertex>());
        for (int x = 0; x < map.cols - 1; ++x)
        {
            Vertex vertex;
            if (getVertexAt(map, x, y, vertex))
            {
                vertices[y].push_back(vertex);
                indices[x + y * map.cols] = vertexCount;
                ++vertexCount;
            }
        }
    }

    // Second pass: create faces for the bottom part
    vector<int> verticesFaceCount(vertexCount, 0);
    for (int y = 0; y < map.rows - 1; ++y)
    {
        for (int x = 0; x < map.cols - 1; ++x)
        {
            auto index = y * map.cols + x;

            auto vertex = indices[index];
            auto next = indices[index + 1];
            auto bottom = indices[index + map.cols];
            auto bottomNext = indices[index + map.cols + 1];

            if (vertex != -1)
            {
                if (next != -1)
                {
                    if (bottom != -1)
                    {
                        Face face;
                        face.indices.push_back(vertex);
                        face.indices.push_back(next);
                        face.indices.push_back(bottom);
                        mesh.faces.push_back(face);

                        verticesFaceCount[vertex]++;
                        verticesFaceCount[next]++;
                        verticesFaceCount[bottom]++;

                        if (bottomNext != -1)
                        {
                            Face face;
                            face.indices.push_back(next);
                            face.indices.push_back(bottomNext);
                            face.indices.push_back(bottom);
                            mesh.faces.push_back(face);

                            verticesFaceCount[next]++;
                            verticesFaceCount[bottomNext]++;
                            verticesFaceCount[bottom]++;
                        }
                    }
                    else if (bottomNext != -1)
                    {
                        Face face;
                        face.indices.push_back(vertex);
                        face.indices.push_back(next);
                        face.indices.push_back(bottomNext);
                        mesh.faces.push_back(face);

                        verticesFaceCount[vertex]++;
                        verticesFaceCount[next]++;
                        verticesFaceCount[bottomNext]++;
                    }
                }
                else
                {
                    if (bottom != -1 && bottomNext != -1)
                    {
                        Face face;
                        face.indices.push_back(vertex);
                        face.indices.push_back(bottom);
                        face.indices.push_back(bottomNext);
                        mesh.faces.push_back(face);

                        verticesFaceCount[vertex]++;
                        verticesFaceCount[bottom]++;
                        verticesFaceCount[bottomNext]++;
                    }
                }
            }
            else
            {
                if (next != -1 && bottom != -1 && bottomNext != -1)
                {
                    Face face;
                    face.indices.push_back(next);
                    face.indices.push_back(bottomNext);
                    face.indices.push_back(bottom);
                    mesh.faces.push_back(face);

                    verticesFaceCount[next]++;
                    verticesFaceCount[bottomNext]++;
                    verticesFaceCount[bottom]++;
                }
            }
        }
    }

    // Third pass: fill mesh.vertices
    for (int y = 0; y < vertices.size(); ++y)
    {
        if (vertices[y].size() == 0)
            continue;

        for (int x = 0; x < vertices[y].size(); ++x)
        {
            vertices[y][x].x *= ratio;
            vertices[y][x].y *= ratio;
            mesh.vertices.push_back(vertices[y][x]);
        }
    }

    // Fourth pass: go through all faces, and extrude them along outter edges
    int faceCount = mesh.faces.size();
    for (int i = 0; i < faceCount; ++i)
    {
        vector<int> outter;
        for (auto idx : mesh.faces[i].indices)
        {
            if (verticesFaceCount[idx] < 6)
                outter.push_back(idx);
            else
                outter.push_back(-1);
        }

        for (int j = 0; j < outter.size(); ++j)
        {
            auto curr = outter[j];
            auto next = outter[(j + 1) % outter.size()];
            if (curr >= 0 && next >= 0)
            {
                Face face;
                face.indices.push_back(curr);
                face.indices.push_back(next);

                mesh.vertices.push_back(mesh.vertices[next]);
                mesh.vertices[mesh.vertices.size() - 1].z = height;
                face.indices.push_back(mesh.vertices.size() - 1);

                mesh.vertices.push_back(mesh.vertices[curr]);
                mesh.vertices[mesh.vertices.size() - 1].z = height;
                face.indices.push_back(mesh.vertices.size() - 1);

                mesh.faces.push_back(face);
            }
        }
    }

    // Fifth pass: fill the top
    for (int i = 0; i < faceCount; ++i)
    {
        Face face;

        for (int j = 0; j < mesh.faces[i].indices.size(); ++j)
        {
            auto vertex = mesh.vertices[mesh.faces[i].indices[j]];
            vertex.z = height;
            mesh.vertices.push_back(vertex);
            face.indices.push_back(mesh.vertices.size() - 1);
        }

        mesh.faces.push_back(face);
    }

    return mesh.faces.size();
}

/*************/
bool writeSTL(const string& filename, const Mesh& mesh)
{
    ofstream file(filename, ios::binary);
    if (!file.is_open())
        return false;

    file << "solid doodle" << endl;
    for (auto& f : mesh.faces)
    {
        if (f.indices.size() == 3)
        {
            file << "facet normal 0.0 0.0 0.0" << endl;
            file << "  outer loop" << endl;
            for (auto i : f.indices)
            {
                auto& vertex = mesh.vertices[i];
                file << "    vertex " << vertex.x << " " << vertex.y << " " << vertex.z << endl;
            }
            file << "  endloop" << endl;
            file << "endfacet" << endl;
        }
        else if (f.indices.size() == 4)
        {
            file << "facet normal 0.0 0.0 0.0" << endl;
            file << "  outer loop" << endl;
            for (int i = 0; i < 3; ++i)
            {
                auto& vertex = mesh.vertices[f.indices[i]];
                file << "    vertex " << vertex.x << " " << vertex.y << " " << vertex.z << endl;
            }
            file << "  endloop" << endl;
            file << "endfacet" << endl;

            file << "facet normal 0.0 0.0 0.0" << endl;
            file << "  outer loop" << endl;
            for (int i = 2; i != 1;)
            {
                i = i % 4;
                auto& vertex = mesh.vertices[f.indices[i]];
                file << "    vertex " << vertex.x << " " << vertex.y << " " << vertex.z << endl;
                ++i;
            }
            file << "  endloop" << endl;
            file << "endfacet" << endl;
        }
    }
    file << "endsolid doodle" << endl;
    file.close();
}

/*************/
bool writeBinarySTL(const string& filename, const Mesh& mesh)
{
    int fd = open(filename.c_str(), O_WRONLY | O_CREAT, 0777);
    if (fd == -1)
    {
        cout << "Error while creating the output STL file" << endl;
        return false;
    }

    auto header = string("Tinkered with love by TinkTank");
    header.resize(80, ' ');
    if (write(fd, header.data(), header.size()) == -1)
    {
        cout << "Error while writing to " << filename << endl;
        return false;
    }

    uint32_t faceNumber = 0;
    for (auto& f : mesh.faces)
        if (f.indices.size() == 3)
            faceNumber += 1;
        else if (f.indices.size() == 4)
            faceNumber += 2;

    if (write(fd, &faceNumber, 4) == -1)
    {
        cout << "Error while writing to " << filename << endl;
        return false;
    }

    for (auto& f : mesh.faces)
    {
        uint16_t attrCount = 0;
        if (f.indices.size() == 3)
        {
            float normal = 0.f;
            for (int i = 0; i < 3; ++i)
                write(fd, &normal, 4);

            for (auto i : f.indices)
            {
                auto& vertex = mesh.vertices[i];
                write(fd, &vertex, 3 * sizeof(float));
            }

            write(fd, &attrCount, sizeof(uint16_t));
        }
        else if (f.indices.size() == 4)
        {
            float normal = 0.f;
            for (int i = 0; i < 3; ++i)
                write(fd, &normal, 4);

            for (int i = 0; i < 3; ++i)
            {
                auto& vertex = mesh.vertices[f.indices[i]];
                write(fd, &vertex, 3 * sizeof(float));
            }
            write(fd, &attrCount, sizeof(uint16_t));

            for (int i = 0; i < 3; ++i)
                write(fd, &normal, 4);
            for (int i = 2; i != 1;)
            {
                i = i % 4;
                auto& vertex = mesh.vertices[f.indices[i]];
                write(fd, &vertex, 3 * sizeof(float));
                ++i;
            }
            write(fd, &attrCount, sizeof(uint16_t));
        }
    }

    fsync(fd);
    close(fd);

    return true;
}

/*************/
void printHelp()
{
    cout << "Usage: doodle2stl --resolution 256 --height 1 [filename]" << endl;
}

/*************/
string getTimeAsString()
{
    static string previousTime = "";
    static int index = 0;

    auto t = std::time(nullptr);
    char charTime[64];
    strftime(charTime, sizeof(charTime), "%y%m%d-%H%M%S", localtime(&t));
    auto currentTime = string(charTime);

    if (previousTime == currentTime)
        ++index;
    else
        index = 0;
    previousTime = currentTime;
    currentTime = currentTime + "_" + to_string(index);

    return currentTime;
}

/*************/
int main(int argc, char** argv)
{
    string filename{""};
    int resolution{256};
    float height{1.f};
    string prefix{"/var/www/doodle2stl"};

    // Fill the parameters
    for (int i = 1; i < argc;)
    {
        if (i < argc - 1 && (string(argv[i]) == "-r" || string(argv[i]) == "--resolution"))
        {
            resolution = stoi(string(argv[i + 1]));
            if (resolution == 0)
                resolution = 128;
            i += 2;
        }
        else if (i < argc - 1 && (string(argv[i]) == "-h" || string(argv[i]) == "--height"))
        {
            height = stof(string(argv[i + 1]));
            if (height <= 1.f)
                height = 1.f;
            i += 2;
        }
        else if (i < argc - 1 && (string(argv[i]) == "-p" || string(argv[i]) == "--prefix"))
        {
            prefix = string(argv[i + 1]);
            i += 2;
        }
        else if (string(argv[i]) == "--help")
        {
            printHelp();
            exit(0);
        }
        else if (i == argc - 1)
        {
            filename = string(argv[i]);
            ++i;
        }
        else
        {
            ++i;
        }
    }

    if (filename.empty())
    {
        printHelp();
        exit(0);
    }

    // Load the image, and resize it
    auto image = cv::imread(filename, cv::IMREAD_GRAYSCALE);
    cv::Mat resized;
    cv::resize(image, resized, cv::Size(2048, (2048 * image.rows) / image.cols));
    image = resized;

    // Apply some filters on it
    filterImage(image);

    // Save the image for debug
    // cv::imwrite("debug.png", image);

    // Create the base 2D mesh from the binary image
    Mesh mesh2D;
    auto nbrFaces = binaryToMesh(image, mesh2D, resolution, height);
    auto path = getTimeAsString() + ".stl";
    cout << path << endl;
    path = prefix + "/" + path;
    auto successWrite = writeBinarySTL(path, mesh2D);

    return 0;
}
