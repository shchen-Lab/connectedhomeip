/**
 * Copyright (c) 2016-2021 Bouffalolab Co., Ltd.
 *
 * Contact information:
 * web site:    https://www.bouffalolab.com/
 */

#include "demo_pwm.h"
#include "board.h"
#include <bl_pwm.h>
#include <stdio.h>
#include <FreeRTOS.h>
#include <task.h>
#include <timers.h>
#include <semphr.h>

#define PWM_FREQ 600
#define PWM_DUTY_CYCLE 10000


static TimerHandle_t Light_TimerHdl=NULL;
volatile static char Light_Timer_Status = 0;
static void demo_hosal_pwm_task_init(void);
static void Light_TimerHandler(TimerHandle_t p_timerhdl);
static void hard_set_color(uint8_t currLevel, uint8_t currHue, uint8_t currSat);
static uint32_t new_Rduty=0;
static uint32_t new_Gduty=0;
static uint32_t new_Bduty=0;
static uint32_t new_Cduty=0;
static uint32_t new_Wduty=0;

static uint32_t Rduty=0;
static uint32_t Gduty=0;
static uint32_t Bduty=0;
static uint32_t Cduty=0;
static uint32_t Wduty=0;

const float pwm_curve[]={0,10.0, 10.015, 10.03, 10.045, 10.061, 10.076, 10.091, 10.106, 10.122, 10.137, 10.152, 10.168, 10.183, 10.198, 10.214, 10.229, 10.245, 10.26, 10.276, 10.291, 10.307, 10.323, 10.338, 10.354, 10.37, 10.385, 10.401, 10.417, 10.432, 10.448, 10.464, 10.48, 10.496, 10.512, 10.527, 10.543, 10.559, 10.575, 10.591, 10.607, 10.623, 10.639, 10.656, 10.672, 10.688, 10.704, 10.72, 10.736, 10.753, 10.769, 10.785, 10.802, 10.818, 10.834, 10.851, 10.867, 10.884, 10.9, 10.916, 10.933, 10.95, 10.966, 10.983, 10.999, 11.016, 11.033, 11.049, 11.066, 11.083, 11.1, 11.116, 11.133, 11.15, 11.167, 11.184, 11.201, 11.218, 11.235, 11.252, 11.269, 11.286, 11.303, 11.32, 11.337, 11.354, 11.371, 11.389, 11.406, 11.423, 11.44, 11.458, 11.475, 11.492, 11.51, 11.527, 11.545, 11.562, 11.58, 11.597, 11.615, 11.632, 11.65, 11.667, 11.685, 11.703, 11.72, 11.738, 11.756, 11.774, 11.792, 11.809, 11.827, 11.845, 11.863, 11.881, 11.899, 11.917, 11.935, 11.953, 11.971, 11.989, 6.007, 6.026, 6.044, 6.062, 6.08, 6.098, 6.117, 6.135, 6.153, 6.172, 6.19, 6.209, 6.227, 6.246, 6.264, 6.283, 6.301, 6.32, 6.339, 6.357, 6.376, 6.395, 6.413, 6.432, 6.451, 6.47, 6.489, 6.508, 6.527, 6.546, 6.565, 6.584, 6.603, 6.622, 6.641, 6.66, 6.679, 6.698, 6.717, 6.737, 6.756, 6.775, 6.795, 6.814, 6.833, 6.853, 6.872, 6.892, 6.911, 6.931, 6.95, 6.97, 6.989, 13.009, 13.029, 13.049, 13.068, 13.088, 13.108, 13.128, 13.148, 13.167, 13.187, 13.207, 13.227, 13.247, 13.267, 13.287, 13.308, 13.328, 13.348, 13.368, 13.388, 13.408, 13.429, 13.449, 13.469, 13.49, 13.51, 13.531, 13.551, 13.572, 13.592, 13.613, 13.633, 13.654, 13.675, 13.695, 13.716, 13.737, 13.758, 13.778, 13.799, 13.82, 13.841, 13.862, 13.883, 13.904, 13.925, 13.946, 13.967, 13.988, 14.009, 14.031, 14.052, 14.073, 14.094, 14.116, 14.137, 14.159, 14.18, 14.201, 14.223, 14.244, 14.266, 14.288, 14.309, 14.331, 14.352, 14.374, 14.396, 14.418, 14.44, 14.461, 14.483, 14.505, 14.527, 14.549, 14.571, 14.593, 14.615, 14.637, 14.66, 14.682, 14.704, 14.726, 14.748, 14.771, 14.793, 14.815, 14.838, 14.86, 14.883, 14.905, 14.928, 14.95, 14.973, 14.996, 15.018, 15.041, 15.064, 15.087, 15.11, 15.132, 15.155, 15.178, 15.201, 15.224, 15.247, 15.27, 15.293, 15.317, 15.34, 15.363, 15.386, 15.409, 15.433, 15.456, 15.48, 15.503, 15.526, 15.55, 15.573, 15.597, 15.621, 15.644, 15.668, 15.692, 15.715, 15.739, 15.763, 15.787, 15.811, 15.835, 15.859, 15.883, 15.907, 15.931, 15.955, 15.979, 16.003, 16.027, 16.051, 16.076, 16.1, 16.124, 16.149, 16.173, 16.198, 16.222, 16.247, 16.271, 16.296, 16.321, 16.345, 16.37, 16.395, 16.42, 16.445, 16.469, 16.494, 16.519, 16.544, 16.569, 16.594, 16.619, 16.645, 16.67, 16.695, 16.72, 16.746, 16.771, 16.796, 16.822, 16.847, 16.873, 16.898, 16.924, 16.949, 16.975, 17.001, 17.026, 17.052, 17.078, 17.104, 17.13, 17.156, 17.182, 17.208, 17.234, 17.26, 17.286, 17.312, 17.338, 17.364, 17.391, 17.417, 17.443, 17.47, 17.496, 17.523, 17.549, 17.576, 17.602, 17.629, 17.656, 17.682, 17.709, 17.736, 17.763, 17.79, 17.816, 17.843, 17.87, 17.897, 17.925, 17.952, 17.979, 18.006, 18.033, 18.061, 18.088, 18.115, 18.143, 18.17, 18.198, 18.225, 18.253, 18.28, 18.308, 18.336, 18.363, 18.391, 18.419, 18.447, 18.475, 18.503, 18.531, 18.559, 18.587, 18.615, 18.643, 18.671, 18.7, 18.728, 18.756, 18.785, 18.813, 18.841, 18.87, 18.899, 18.927, 18.956, 18.984, 19.013, 19.042, 19.071, 19.1, 19.128, 19.157, 19.186, 19.215, 19.245, 19.274, 19.303, 19.332, 19.361, 19.391, 19.42, 19.449, 19.479, 19.508, 19.538, 19.567, 19.597, 19.627, 19.656, 19.686, 19.716, 19.746, 19.775, 19.805, 19.835, 19.865, 19.895, 19.925, 19.956, 19.986, 20.016, 20.046, 20.077, 20.107, 20.137, 20.168, 20.198, 20.229, 20.26, 20.29, 20.321, 20.352, 20.383, 20.413, 20.444, 20.475, 20.506, 20.537, 20.568, 20.599, 20.631, 20.662, 20.693, 20.724, 20.756, 20.787, 20.819, 20.85, 20.882, 20.913, 20.945, 20.977, 21.008, 21.04, 21.072, 21.104, 21.136, 21.168, 21.2, 21.232, 21.264, 21.296, 21.328, 21.361, 21.393, 21.425, 21.458, 21.49, 21.523, 21.555, 21.588, 21.62, 21.653, 21.686, 21.719, 21.752, 21.785, 21.818, 21.851, 21.884, 21.917, 21.95, 21.983, 22.016, 22.05, 22.083, 22.116, 22.15, 22.183, 22.217, 22.251, 22.284, 22.318, 22.352, 22.386, 22.419, 22.453, 22.487, 22.521, 22.555, 22.59, 22.624, 22.658, 22.692, 22.727, 22.761, 22.795, 22.83, 22.864, 22.899, 22.934, 22.968, 23.003, 23.038, 23.073, 23.108, 23.143, 23.178, 23.213, 23.248, 23.283, 23.318, 23.353, 23.389, 23.424, 23.46, 23.495, 23.531, 23.566, 23.602, 23.638, 23.673, 23.709, 23.745, 23.781, 23.817, 23.853, 23.889, 23.925, 23.962, 23.998, 24.034, 24.07, 24.107, 24.143, 24.18, 24.216, 24.253, 24.29, 24.327, 24.363, 24.4, 24.437, 24.474, 24.511, 24.548, 24.585, 24.623, 24.66, 24.697, 24.734, 24.772, 24.809, 24.847, 24.885, 24.922, 24.96, 24.998, 25.035, 25.073, 25.111, 25.149, 25.187, 25.225, 25.264, 25.302, 25.34, 25.378, 25.417, 25.455, 25.494, 25.532, 25.571, 25.61, 25.648, 25.687, 25.726, 25.765, 25.804, 25.843, 25.882, 25.921, 25.961, 26.0, 26.039, 26.079, 26.118, 26.158, 26.197, 26.237, 26.276, 26.316, 26.356, 26.396, 26.436, 26.476, 26.516, 26.556, 26.596, 26.636, 26.677, 26.717, 26.758, 26.798, 26.839, 26.879, 26.92, 26.961, 27.001, 27.042, 27.083, 27.124, 27.165, 27.206, 27.247, 27.289, 27.33, 27.371, 27.413, 27.454, 27.496, 27.537, 27.579, 27.621, 27.662, 27.704, 27.746, 27.788, 27.83, 27.872, 27.915, 27.957, 27.999, 28.041, 28.084, 28.126, 28.169, 28.212, 28.254, 28.297, 28.34, 28.383, 28.426, 28.469, 28.512, 28.555, 28.598, 28.641, 28.685, 28.728, 28.772, 28.815, 28.859, 28.902, 28.946, 28.99, 29.034, 29.078, 29.122, 29.166, 29.21, 29.254, 29.298, 29.343, 29.387, 29.431, 29.476, 29.521, 29.565, 29.61, 29.655, 29.7, 29.745, 29.79, 29.835, 29.88, 29.925, 29.97, 30.016, 30.061, 30.107, 30.152, 30.198, 30.243, 30.289, 30.335, 30.381, 30.427, 30.473, 30.519, 30.565, 30.611, 30.658, 30.704, 30.751, 30.797, 30.844, 30.89, 30.937, 30.984, 31.031, 31.078, 31.125, 31.172, 31.219, 31.266, 31.314, 31.361, 31.408, 31.456, 31.503, 31.551, 31.599, 31.647, 31.695, 31.743, 31.791, 31.839, 31.887, 31.935, 31.983, 32.032, 32.08, 32.129, 32.177, 32.226, 32.275, 32.324, 32.373, 32.422, 32.471, 32.52, 32.569, 32.618, 32.668, 32.717, 32.767, 32.816, 32.866, 32.915, 32.965, 33.015, 33.065, 33.115, 33.165, 33.215, 33.266, 33.316, 33.366, 33.417, 33.467, 33.518, 33.569, 33.62, 33.67, 33.721, 33.772, 33.824, 33.875, 33.926, 33.977, 34.029, 34.08, 34.132, 34.183, 34.235, 34.287, 34.339, 34.391, 34.443, 34.495, 34.547, 34.599, 34.652, 34.704, 34.757, 34.809, 34.862, 34.915, 34.967, 35.02, 35.073, 35.126, 35.18, 35.233, 35.286, 35.339, 35.393, 35.446, 35.5, 35.554, 35.608, 35.661, 35.715, 35.769, 35.824, 35.878, 35.932, 35.986, 36.041, 36.095, 36.15, 36.205, 36.26, 36.314, 36.369, 36.424, 36.479, 36.535, 36.59, 36.645, 36.701, 36.756, 36.812, 36.868, 36.923, 36.979, 37.035, 37.091, 37.147, 37.204, 37.26, 37.316, 37.373, 37.429, 37.486, 37.543, 37.599, 37.656, 37.713, 37.77, 37.827, 37.885, 37.942, 37.999, 38.057, 38.114, 38.172, 38.23, 38.288, 38.346, 38.404, 38.462, 38.52, 38.578, 38.637, 38.695, 38.754, 38.812, 38.871, 38.93, 38.989, 39.048, 39.107, 39.166, 39.225, 39.285, 39.344, 39.404, 39.463, 39.523, 39.583, 39.643, 39.703, 39.763, 39.823, 39.883, 39.943, 40.004, 40.064, 40.125, 40.186, 40.246, 40.307, 40.368, 40.429, 40.491, 40.552, 40.613, 40.675, 40.736, 40.798, 40.86, 40.921, 40.983, 41.045, 41.107, 41.17, 41.232, 41.294, 41.357, 41.419, 41.482, 41.545, 41.608, 41.671, 41.734, 41.797, 41.86, 41.923, 41.987, 42.05, 42.114, 42.178, 42.241, 42.305, 42.369, 42.433, 42.498, 42.562, 42.626, 42.691, 42.755, 42.82, 42.885, 42.95, 43.015, 43.08, 43.145, 43.21, 43.276, 43.341, 43.407, 43.472, 43.538, 43.604, 43.67, 43.736, 43.802, 43.869, 43.935, 44.001, 44.068, 44.135, 44.201, 44.268, 44.335, 44.402, 44.47, 44.537, 44.604, 44.672, 44.739, 44.807, 44.875, 44.943, 45.011, 45.079, 45.147, 45.215, 45.284, 45.352, 45.421, 45.49, 45.558, 45.627, 45.696, 45.766, 45.835, 45.904, 45.974, 46.043, 46.113, 46.183, 46.252, 46.322, 46.393, 46.463, 46.533, 46.603, 46.674, 46.745, 46.815, 46.886, 46.957, 47.028, 47.099, 47.17, 47.242, 47.313, 47.385, 47.457, 47.528, 47.6, 47.672, 47.744, 47.817, 47.889, 47.962, 48.034, 48.107, 48.18, 48.252, 48.325, 48.399, 48.472, 48.545, 48.619, 48.692, 48.766, 48.84, 48.914, 48.988, 49.062, 49.136, 49.21, 49.285, 49.359, 49.434, 49.509, 49.584, 49.659, 49.734, 49.809, 49.884, 49.96, 50.035, 50.111, 50.187, 50.263, 50.339, 50.415, 50.491, 50.568, 50.644, 50.721, 50.798, 50.875, 50.951, 51.029, 51.106, 51.183, 51.261, 51.338, 51.416, 51.494, 51.571, 51.65, 51.728, 51.806, 51.884, 51.963, 52.041, 52.6, 52.199, 52.278, 52.357, 52.436, 52.516, 52.595, 52.675, 52.754, 52.834, 52.914, 52.994, 53.074, 53.155, 53.235, 53.316, 53.396, 53.477, 53.558, 53.639, 53.72, 53.802, 53.883, 53.964, 54.046, 54.128, 54.21, 54.292, 54.374, 54.456, 54.539, 54.621, 54.704, 54.787, 54.869, 54.952, 55.036, 55.119, 55.202, 55.286, 55.369, 55.453, 55.537, 55.621, 55.705, 55.79, 55.874, 55.958, 56.043, 56.128, 56.213, 56.298, 56.383, 56.468, 56.554, 56.639, 56.725, 56.811, 56.897, 56.983, 57.069, 57.156, 57.242, 57.329, 57.415, 57.502, 57.589, 57.676, 57.764, 57.851, 57.939, 58.026, 58.114, 58.202, 58.29, 58.378, 58.467, 58.555, 58.644, 58.732, 58.821, 58.91, 58.999, 59.089, 59.178, 59.268, 59.357, 59.447, 59.537, 59.627, 59.717, 59.808, 59.898, 59.989, 60.079, 60.17, 60.261, 60.353, 60.444, 60.535, 60.627, 60.719, 60.811, 60.903, 60.995, 61.087, 61.179, 61.272, 61.365, 61.458, 61.551, 61.644, 61.737, 61.83, 61.924, 62.018, 62.111, 62.205, 62.3, 62.394, 62.488, 62.583, 62.677, 62.772, 62.867, 62.962, 63.058, 63.153, 63.249, 63.344, 63.44, 63.536, 63.632, 63.728, 63.825, 63.921, 64.018, 64.115, 64.212, 64.309, 64.407, 64.504, 64.602, 64.699, 64.797, 64.895, 64.993, 65.092, 65.19, 65.289, 65.388, 65.487, 65.586, 65.685, 65.784, 65.884, 65.983, 66.083, 66.183, 66.283, 66.384, 66.484, 66.585, 66.686, 66.786, 66.887, 66.989, 67.09, 67.192, 67.293, 67.395, 67.497, 67.599, 67.701, 67.804, 67.906, 68.009, 68.112, 68.215, 68.318, 68.422, 68.525, 68.629, 68.733, 68.837, 68.941, 69.045, 69.15, 69.254, 69.359, 69.464, 69.569, 69.674, 69.78, 69.885, 69.991, 70.097, 70.203, 70.309, 70.416, 70.522, 70.629, 70.736, 70.843, 70.95, 71.057, 71.165, 71.272, 71.38, 71.488, 71.596, 71.705, 71.813, 71.922, 72.031, 72.14, 72.249, 72.358, 72.468, 72.577, 72.687, 72.797, 72.907, 73.018, 73.128, 73.239, 73.35, 73.46, 73.572, 73.683, 73.794, 73.906, 74.018, 74.13, 74.242, 74.354, 74.467, 74.58, 74.692, 74.805, 74.919, 75.032, 75.145, 75.259, 75.373, 75.487, 75.601, 75.716, 75.83, 75.945, 76.06, 76.175, 76.29, 76.406, 76.521, 76.637, 76.753, 76.869, 76.985, 77.102, 77.219, 77.335, 77.452, 77.57, 77.687, 77.804, 77.922, 78.04, 78.158, 78.276, 78.395, 78.513, 78.632, 78.751, 78.87, 78.99, 79.109, 79.229, 79.349, 79.469, 79.589, 79.71, 79.83, 79.951, 80.072, 80.193, 80.314, 80.436, 80.558, 80.679, 80.802, 80.924, 81.046, 81.169, 81.292, 81.415, 81.538, 81.661, 81.785, 81.908, 82.032, 82.157, 82.281, 82.405, 82.53, 82.655, 82.78, 82.905, 83.031, 83.156, 83.282, 83.408, 83.534, 83.661, 83.787, 83.914, 84.041, 84.168, 84.295, 84.423, 84.551, 84.679, 84.807, 84.935, 85.064, 85.192, 85.321, 85.45, 85.58, 85.709, 85.839, 85.969, 86.099, 86.229, 86.359, 86.49, 86.621, 86.752, 86.883, 87.015, 87.146, 87.278, 87.41, 87.543, 87.675, 87.808, 87.941, 88.074, 88.207, 88.34, 88.474, 88.608, 88.742, 88.876, 89.011, 89.145, 89.28, 89.415, 89.551, 89.686, 89.822, 89.958, 90.094, 90.23, 90.367, 90.503, 90.64, 90.777, 90.915, 91.052, 91.19, 91.328, 91.466, 91.605, 91.743, 91.882, 92.021, 92.16, 92.3, 92.439, 92.579, 92.719, 92.86, 93.0, 93.141, 93.282, 93.423, 93.564, 93.706, 93.848, 93.99, 94.132, 94.274, 94.417, 94.56, 94.703, 94.846, 94.99, 95.133, 95.277, 95.421, 95.566, 95.71, 95.855, 96.0, 96.145, 96.291, 96.437, 96.582, 96.729, 96.875, 97.022, 97.168, 97.315, 97.463, 97.61, 97.758, 97.906, 98.054, 98.202, 98.351, 98.499, 98.649, 98.798, 98.947, 99.097, 99.247, 99.397, 99.547, 99.698, 99.849, 100.0};

void demo_hosal_pwm_init(void)
{
    
    bl_pwm_port_init(LED_R_PIN_PORT, PWM_FREQ);
    bl_pwm_port_init(LED_B_PIN_PORT, PWM_FREQ);
    bl_pwm_port_init(LED_G_PIN_PORT, PWM_FREQ);
    bl_pwm_port_init(LED_C_PIN_PORT, PWM_FREQ);
    bl_pwm_port_init(LED_W_PIN_PORT, PWM_FREQ);

    bl_pwm_channel_init(LED_R_PIN_PORT, LED_R_PIN);
    bl_pwm_channel_init(LED_B_PIN_PORT, LED_B_PIN);
    bl_pwm_channel_init(LED_G_PIN_PORT, LED_G_PIN);
    bl_pwm_channel_init(LED_C_PIN_PORT, LED_C_PIN);
    bl_pwm_channel_init(LED_W_PIN_PORT, LED_W_PIN);
    demo_hosal_pwm_task_init();
}

void demo_hosal_pwm_start(void)
{
    /* start pwm */
    for (uint32_t i = 0; i < MAX_PWM_CHANNEL; i++)
    {
        bl_pwm_start(i);
    }
}

void demo_hosal_pwm_set_param(uint32_t p_Rduty,uint32_t p_Gduty,uint32_t p_Bduty,uint32_t p_Cduty,uint32_t p_Wduty)
{

    uint16_t threshold1, threshold2;
    threshold1 = 0;

    bl_pwm_set_duty(LED_R_PIN_PORT, pwm_curve[p_Rduty]);
    bl_pwm_set_duty(LED_G_PIN_PORT, pwm_curve[p_Gduty]);
    bl_pwm_set_duty(LED_B_PIN_PORT, pwm_curve[p_Bduty]);
    
    bl_pwm_set_duty_ex(LED_C_PIN_PORT, pwm_curve[p_Cduty],&threshold1, &threshold2);
    threshold1 = threshold2;
    bl_pwm_set_duty_ex(LED_W_PIN_PORT, pwm_curve[p_Wduty],&threshold1, &threshold2);
}

void demo_hosal_pwm_stop(void)
{
    for (uint32_t i = 0; i < MAX_PWM_CHANNEL; i++)
    {
        bl_pwm_stop(i);
    }
}

void set_level(uint8_t currLevel)
{
    printf("%s\r\n",__func__);

}

void set_color_red(uint8_t currLevel)
{
    hard_set_color(currLevel, 0, 254);
}

void set_color_green(uint8_t currLevel)
{
    hard_set_color(currLevel, 84, 254);
}

void set_color_yellow(uint8_t currLevel)
{
    hard_set_color(currLevel, 42, 254);
}

static void hard_set_color(uint8_t currLevel, uint8_t currHue, uint8_t currSat)
{
#if MAX_PWM_CHANNEL
    uint16_t hue = (uint16_t) currHue * 360 / 254;
    uint8_t sat  = (uint16_t) currSat * 100 / 254;

    if (currLevel <= 5 && currLevel >= 1)
    {
        currLevel = 5; // avoid demo off
    }

    if (sat > 100)
    {
        sat = 100;
    }

    uint16_t i       = hue / 60;
    uint16_t rgb_max = currLevel;
    uint16_t rgb_min = rgb_max * (100 - sat) / 100;
    uint16_t diff    = hue % 60;
    uint16_t rgb_adj = (rgb_max - rgb_min) * diff / 60;
    uint32_t red, green, blue;

    switch (i)
    {
    case 0:
        red   = rgb_max;
        green = rgb_min + rgb_adj;
        blue  = rgb_min;
        break;
    case 1:
        red   = rgb_max - rgb_adj;
        green = rgb_max;
        blue  = rgb_min;
        break;
    case 2:
        red   = rgb_min;
        green = rgb_max;
        blue  = rgb_min + rgb_adj;
        break;
    case 3:
        red   = rgb_min;
        green = rgb_max - rgb_adj;
        blue  = rgb_max;
        break;
    case 4:
        red   = rgb_min + rgb_adj;
        green = rgb_min;
        blue  = rgb_max;
        break;
    default:
        red   = rgb_max;
        green = rgb_min;
        blue  = rgb_max - rgb_adj;
        break;
    }

    Rduty=(red*6);
    Gduty=(green*6);
    Bduty=(blue*6);
    printf("Rduty=%lx,Gduty=%lx,Bduty=%lx\r\n",Rduty,Gduty,Bduty);
    demo_hosal_pwm_set_param(Rduty,Gduty,Bduty,0,0);
#else
    set_level(currLevel);
#endif
}
void set_color(uint8_t currLevel, uint8_t currHue, uint8_t currSat)
{
    printf("%s\r\n",__func__);
    uint16_t hue = (uint16_t)currHue * 360 / 254;
    uint8_t  sat = (uint16_t)currSat * 100 / 254;

    if(sat > 100)
    {
        sat = 100;
    }

    uint16_t i       = hue / 60;
    uint16_t rgb_max = currLevel;
    uint16_t rgb_min = rgb_max * (100 - sat) / 100;
    uint16_t diff    = hue % 60;
    uint16_t rgb_adj = (rgb_max - rgb_min) * diff / 60;
    uint32_t red, green, blue;

    switch (i)
    {
    case 0:
        red   = rgb_max;
        green = rgb_min + rgb_adj;
        blue  = rgb_min;
        break;
    case 1:
        red   = rgb_max - rgb_adj;
        green = rgb_max;
        blue  = rgb_min;
        break;
    case 2:
        red   = rgb_min;
        green = rgb_max;
        blue  = rgb_min + rgb_adj;
        break;
    case 3:
        red   = rgb_min;
        green = rgb_max - rgb_adj;
        blue  = rgb_max;
        break;
    case 4:
        red   = rgb_min + rgb_adj;
        green = rgb_min;
        blue  = rgb_max;
        break;
    default:
        red   = rgb_max;
        green = rgb_min;
        blue  = rgb_max - rgb_adj;
        break;
    }

    new_Rduty=(red*6);
    new_Gduty=(green*6);
    new_Bduty=(blue*6);
    printf("now_Rduty update=%lx,now_Gduty update =%lx,now_Bduty update =%lx\r\n",new_Rduty,new_Gduty,new_Bduty);
    if(Light_TimerHdl!=NULL)
    {
        if( xTimerIsTimerActive( Light_TimerHdl ) != pdFALSE )
        {
            if(Light_TimerHdl)
                xTimerStop(Light_TimerHdl, 0); 
        }
        if( xTimerChangePeriod(Light_TimerHdl, pdMS_TO_TICKS(1), 0 ) == pdPASS )
        {
                
            Light_Timer_Status=1;
            if(Light_TimerHdl)
                xTimerStart(Light_TimerHdl, 0); 
        }

    } 

  
}
void set_temperature(uint8_t currLevel,uint16_t temperature)
{
    printf("%s\r\n",__func__);
    uint32_t hw_temp_delta=LAM_MAX_MIREDS_DEFAULT-LAM_MIN_MIREDS_DEFAULT;
    uint32_t soft_temp_delta;

    if(temperature>LAM_MAX_MIREDS_DEFAULT)
    {
        temperature=LAM_MAX_MIREDS_DEFAULT;
    }
    else if(temperature<LAM_MIN_MIREDS_DEFAULT)
    {
        temperature=LAM_MIN_MIREDS_DEFAULT;
    }
    
    soft_temp_delta=temperature-LAM_MIN_MIREDS_DEFAULT;
    soft_temp_delta*=100;

    uint32_t warm = (254*(soft_temp_delta/hw_temp_delta))/100;
    uint32_t clod  = 254-warm;
    new_Wduty=warm*6*currLevel/254;
    new_Cduty=clod*6*currLevel/254;
    printf("now_Cduty update=%lx,now_Wduty update =%lx\r\n",new_Cduty,new_Wduty);
     if(Light_TimerHdl!=NULL)
    {
        if( xTimerIsTimerActive( Light_TimerHdl ) != pdFALSE )
        {
            if(Light_TimerHdl)
                xTimerStop(Light_TimerHdl, 0); 
        }
        if( xTimerChangePeriod(Light_TimerHdl, pdMS_TO_TICKS(1), 0 ) == pdPASS )
        {
                
            Light_Timer_Status=2;
            if(Light_TimerHdl)
                xTimerStart(Light_TimerHdl, 0); 
        }

    }
}
void set_warm_temperature()
{
    uint16_t threshold1, threshold2;
    threshold1 = 0;
    Rduty=0;
    Gduty=0;
    Bduty=0;
    Wduty=254*6;
    Cduty=0;

    bl_pwm_set_duty(LED_R_PIN_PORT, pwm_curve[Rduty]);
    bl_pwm_set_duty(LED_G_PIN_PORT, pwm_curve[Gduty]);
    bl_pwm_set_duty(LED_B_PIN_PORT, pwm_curve[Bduty]);
    
    bl_pwm_set_duty_ex(LED_C_PIN_PORT, pwm_curve[Cduty],&threshold1, &threshold2);
    threshold1 = threshold2;
    bl_pwm_set_duty_ex(LED_W_PIN_PORT, pwm_curve[Wduty],&threshold1, &threshold2);
 
}

void set_cold_temperature(void)
{
    uint16_t threshold1, threshold2;
    threshold1 = 0;
    Rduty=0;
    Gduty=0;
    Bduty=0;
    Wduty=0;
    Cduty=254*6;

    bl_pwm_set_duty(LED_R_PIN_PORT, pwm_curve[Rduty]);
    bl_pwm_set_duty(LED_G_PIN_PORT, pwm_curve[Gduty]);
    bl_pwm_set_duty(LED_B_PIN_PORT, pwm_curve[Bduty]);
    
    bl_pwm_set_duty_ex(LED_C_PIN_PORT, pwm_curve[Cduty],&threshold1, &threshold2);
    threshold1 = threshold2;
    bl_pwm_set_duty_ex(LED_W_PIN_PORT, pwm_curve[Wduty],&threshold1, &threshold2);
}
void set_warm_cold_off(void)
{
    uint16_t threshold1, threshold2;
    threshold1 = 0;
    Rduty=0;
    Gduty=0;
    Bduty=0;
    Wduty=0;
    Cduty=0;

    bl_pwm_set_duty(LED_R_PIN_PORT, pwm_curve[Rduty]);
    bl_pwm_set_duty(LED_G_PIN_PORT, pwm_curve[Gduty]);
    bl_pwm_set_duty(LED_B_PIN_PORT, pwm_curve[Bduty]);
    
    bl_pwm_set_duty_ex(LED_C_PIN_PORT, pwm_curve[Cduty],&threshold1, &threshold2);
    threshold1 = threshold2;
    bl_pwm_set_duty_ex(LED_W_PIN_PORT, pwm_curve[Wduty],&threshold1, &threshold2);
}
static void Light_TimerHandler(TimerHandle_t p_timerhdl)
{

	if( Light_Timer_Status==1)
	{
		if((new_Rduty!=Rduty)||(new_Gduty!=Gduty)||(new_Bduty!=Bduty))
		{
			if(new_Rduty>Rduty)
			{
				Rduty+=2;
			}
			else if(new_Rduty<Rduty)
			{
				Rduty-=2;
			}


			if(new_Gduty>Gduty)
			{
				Gduty+=2;
			}
			else if(new_Gduty<Gduty)
			{
				Gduty-=2;
			}

			if(new_Bduty>Bduty)
			{
				Bduty+=2;
			}
			else if(new_Bduty<Bduty)
			{
				Bduty-=2;
			}
            demo_hosal_pwm_set_param(Rduty,Gduty,Bduty,0,0);
		}  
		else
		{
		    if(Light_TimerHdl)
            {
                xTimerStop(Light_TimerHdl, 0); 
            }
		    Light_Timer_Status=0;
		}
	}
	else if(Light_Timer_Status==2)
	{
		if((new_Cduty!=Cduty)||(new_Wduty!=Wduty))
		{
			if(new_Cduty>Cduty)
			{
				Cduty+=2;
			}
			else if(new_Cduty<Cduty)
			{
				Cduty-=2;
			}


			if(new_Wduty>Wduty)
			{
				Wduty+=2;
			}
			else if(new_Wduty<Wduty)
			{
				Wduty-=2;
			}
            demo_hosal_pwm_set_param(0,0,0,Cduty,Wduty);
		}
		else
		{
		    if(Light_TimerHdl)
            {
                xTimerStop(Light_TimerHdl, 0); 
            } 
		     Light_Timer_Status=0;
		}
           
            
	}

                   
}
static void demo_hosal_pwm_task_init(void)
{

    Light_TimerHdl = xTimerCreate("zb_Light_TimerHandler", pdMS_TO_TICKS(2), pdTRUE, NULL,  Light_TimerHandler);
}