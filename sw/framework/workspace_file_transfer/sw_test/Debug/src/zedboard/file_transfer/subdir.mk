################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
../src/zedboard/file_transfer/networking.cpp \
../src/zedboard/file_transfer/server.cpp 

OBJS += \
./src/zedboard/file_transfer/networking.o \
./src/zedboard/file_transfer/server.o 

CPP_DEPS += \
./src/zedboard/file_transfer/networking.d \
./src/zedboard/file_transfer/server.d 


# Each subdirectory must supply rules for building sources it contributes
src/zedboard/file_transfer/%.o: ../src/zedboard/file_transfer/%.cpp
	@echo 'Building file: $<'
	@echo 'Invoking: ARM v7 g++ compiler'
	arm-none-eabi-g++ -DZEDBOARD -DQUANT_BITS=8 -Wall -O0 -g3 -I../src/framework -I../src/zedboard -c -fmessage-length=0 -MT"$@" -mcpu=cortex-a9 -mfpu=vfpv3 -mfloat-abi=hard -std=c++17 -O3 -I/home/jwk0425/jwk_personal_lab/lab5/sw/framework/workspace_file_transfer/sw_test_platform/export/sw_test_platform/sw/sw_test_platform/standalone_domain/bspinclude/include -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


