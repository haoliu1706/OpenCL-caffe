#include <string>
#include <vector>

#include "caffe/data_layers.hpp"
#include "caffe/util/io.hpp"

namespace caffe {

template <typename Dtype>
BaseDataLayer<Dtype>::BaseDataLayer(const LayerParameter& param)
    : Layer<Dtype>(param),
      transform_param_(param.transform_param()) {
}

template <typename Dtype>
void BaseDataLayer<Dtype>::LayerSetUp(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top) {
  if (top.size() == 1) {
    output_labels_ = false;
  } else {
    output_labels_ = true;
  }
  data_transformer_.reset(
      new DataTransformer<Dtype>(transform_param_, this->phase_));
  data_transformer_->InitRand();
  // The subclasses should setup the size of bottom and top
  DataLayerSetUp(bottom, top);
}

template <typename Dtype>
void BasePrefetchingDataLayer<Dtype>::LayerSetUp(
    const vector<Blob<Dtype>*>& bottom, const vector<Blob<Dtype>*>& top) {
  BaseDataLayer<Dtype>::LayerSetUp(bottom, top);
  // Now, start the prefetch thread. Before calling prefetch, we make two
  // cpu_data calls so that the prefetch thread does not accidentally make
  // simultaneous cudaMalloc calls when the main thread is running. In some
  // GPUs this seems to cause failures if we do not so.
  this->prefetch_data_.mutable_cpu_data();
  if (this->output_labels_) {
    this->prefetch_label_.mutable_cpu_data();
  }
  DLOG(INFO) << "Initializing prefetch";
  this->CreatePrefetchThread();
  DLOG(INFO) << "Prefetch initialized.";
}

template <typename Dtype>
void BasePrefetchingDataLayer<Dtype>::CreatePrefetchThread() {
  this->data_transformer_->InitRand();
  CHECK(StartInternalThread()) << "Thread execution failed";
}

template <typename Dtype>
void BasePrefetchingDataLayer<Dtype>::JoinPrefetchThread() {
  CHECK(WaitForInternalThreadToExit()) << "Thread joining failed";
}

template <typename Dtype>
void BasePrefetchingDataLayer<Dtype>::Forward_cpu(
    const vector<Blob<Dtype>*>& bottom, const vector<Blob<Dtype>*>& top) {
  // First, join the thread
  JoinPrefetchThread();
  DLOG(INFO) << "Thread joined";
  // Reshape to loaded data.
  top[0]->ReshapeLike(prefetch_data_);
  // Copy the data
  caffe_copy(prefetch_data_.count(), prefetch_data_.cpu_data(),
             top[0]->mutable_cpu_data());
  DLOG(INFO) << "Prefetch copied";
  if (this->output_labels_) {
    // Reshape to loaded labels.
    top[1]->ReshapeLike(prefetch_label_);
    // Copy the labels.
    caffe_copy(prefetch_label_.count(), prefetch_label_.cpu_data(),
               top[1]->mutable_cpu_data());
  }

  CHECK_BLOB_DATA(top[0], 20, "top[0]");

  // Start a new prefetch thread
  DLOG(INFO) << "CreatePrefetchThread";
  CreatePrefetchThread();

}

template <typename Dtype>
void BasePrefetchingDataLayer<Dtype>::Forward_gpu(const vector<Blob<Dtype>*>& bottom,
     const  vector<Blob<Dtype>*>& top) {
  // First, join the thread
  JoinPrefetchThread();
  // Copy the data from prefetch thread to data_layer
   //OCL_CHECK( clEnqueueCopyBuffer (amdDevice.CommandQueue, (cl_mem) prefetch_data_->gpu_data(), (cl_mem) (*top)[0]->mutable_gpu_data(), 0, 0, sizeof(Dtype)*prefetch_data_->count(), 0, NULL, NULL) );
   top[0]->ReshapeLike(prefetch_data_);
    OCL_CHECK( clEnqueueWriteBuffer (amdDevice.CommandQueue, (cl_mem)top[0]->mutable_gpu_data(), CL_TRUE, 0, sizeof(Dtype)*prefetch_data_.count(), prefetch_data_.cpu_data(), 0, NULL, NULL) );
  if (this->output_labels_) {
       // Reshape to loaded labels.
    top[1]->ReshapeLike(prefetch_label_);
   OCL_CHECK( clEnqueueWriteBuffer (amdDevice.CommandQueue, (cl_mem)top[1]->mutable_gpu_data(), CL_TRUE, 0, sizeof(Dtype)*prefetch_label_.count(), prefetch_label_.cpu_data(), 0, NULL, NULL) );
   //OCL_CHECK( clEnqueueCopyBuffer (amdDevice.CommandQueue, (cl_mem) prefetch_label_->gpu_data(), (cl_mem) (*top)[1]->mutable_gpu_data(), 0, 0, sizeof(Dtype)*prefetch_label_->count(), 0, NULL, NULL) );
   }
  clFinish(amdDevice.CommandQueue);
  
#ifdef Track_data_transfer
#endif
  
  CHECK_BLOB_DATA(top[0], 20, "top[0]");  

  // Start a new prefetch thread
  DLOG(INFO) << "CreatePrefetchThread";
  CreatePrefetchThread();
  //return Dtype(0.);
}

/*template <typename Dtype>
void BasePrefetchingDataLayer<Dtype>::Forward_gpu(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top){
}*/



#ifdef CPU_ONLY
STUB_GPU_FORWARD(BasePrefetchingDataLayer, Forward);
#endif

INSTANTIATE_CLASS(BaseDataLayer);
INSTANTIATE_CLASS(BasePrefetchingDataLayer);

}  // namespace caffe
