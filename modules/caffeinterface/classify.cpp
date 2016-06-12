/* Caffe Interface for deep learning
 *  Author: federico.corradi@inilabs.com
 */
#include "classify.hpp"
#include "settings.h"

using namespace caffe;
// NOLINT(build/namespaces)
using std::string;

void MyClass::file_set(char * i, double *b, double thr, bool printoutputs,
		caerFrameEvent single_frame) {
	MyClass::file_i = i;

	if (file_i != NULL) {

		//std::cout << "\n---------- Prediction for " << file_i << " started ----------\n" << std::endl;
		cv::Mat img = cv::imread(file_i, 0);
		cv::Mat img2;
		img.convertTo(img2, CV_32FC1);
		img2 = img2 * 0.00390625;
		//std::cout << "\n" << img2 << std::endl;

		CHECK(!img.empty()) << "Unable to decode image " << file_i;
		std::vector<Prediction> predictions = MyClass::Classify(img2, 5,
				single_frame);

		/* Print the top N predictions. */
		for (size_t i = 0; i < predictions.size(); ++i) {
			Prediction p = predictions[i];
			if (printoutputs) {
				std::cout << "\n" << std::fixed << std::setprecision(4)
						<< p.second << " - \"" << p.first << "\"" << std::endl;
			}
			// for face detection net
			if (p.first.compare("FACE") == 0 && p.second > thr) {
				*b = p.second;
				std::cout << "\n" << p.second << " DETECTION " << std::endl;
			}
		}
	}
}

char * MyClass::file_get() {
	return file_i;
}

void MyClass::init_network() {

	//::google::InitGoogleLogging(0);
	string model_file = NET_MODEL
	;
	string trained_file = NET_WEIGHTS
	;
	string mean_file = NET_MEAN
	;
	string label_file = NET_VAL
	;
	MyClass::Classifier(model_file, trained_file, mean_file, label_file);

	return;

}

void MyClass::Classifier(const string& model_file, const string& trained_file,
		const string& mean_file, const string& label_file) {
#ifdef CPU_ONLY
	Caffe::set_mode(Caffe::CPU);
#else
	Caffe::set_mode(Caffe::GPU);
#endif

	/* Load the network. */
	net_.reset(new Net<float>(model_file, TEST));
	net_->CopyTrainedLayersFrom(trained_file);

	CHECK_EQ(net_->num_inputs(), 1) << "Network should have exactly one input.";
	CHECK_EQ(net_->num_outputs(), 1)
			<< "Network should have exactly one output.";

	Blob<float>* input_layer = net_->input_blobs()[0];
	num_channels_ = input_layer->channels();
	CHECK(num_channels_ == 3 || num_channels_ == 1)
			<< "Input layer should have 1 or 3 channels.";
	input_geometry_ = cv::Size(input_layer->width(), input_layer->height());

	/* Load the binaryproto mean file. */
	//SetMean(mean_file);
	/* Load labels. */
	std::ifstream labels(label_file.c_str());
	CHECK(labels) << "Unable to open labels file " << label_file;
	string line;
	while (std::getline(labels, line))
		labels_.push_back(string(line));

	Blob<float>* output_layer = net_->output_blobs()[0];
	CHECK_EQ(labels_.size(), output_layer->channels())
			<< "Number of labels is different from the output layer dimension.";
}

static bool PairCompare(const std::pair<float, int>& lhs,
		const std::pair<float, int>& rhs) {
	return lhs.first > rhs.first;
}

/* Return the indices of the top N values of vector v. */
static std::vector<int> Argmax(const std::vector<float>& v, int N) {
	std::vector<std::pair<float, int> > pairs;
	for (size_t i = 0; i < v.size(); ++i)
		pairs.push_back(std::make_pair(v[i], i));
	std::partial_sort(pairs.begin(), pairs.begin() + N, pairs.end(),
			PairCompare);

	std::vector<int> result;
	for (int i = 0; i < N; ++i)
		result.push_back(pairs[i].second);
	return result;
}

/* Return the top N predictions. */
std::vector<Prediction> MyClass::Classify(const cv::Mat& img, int N,
		caerFrameEvent single_frame) {
	std::vector<float> output = Predict(img, single_frame);

	N = std::min<int>(labels_.size(), N);
	std::vector<int> maxN = Argmax(output, N);
	std::vector<Prediction> predictions;
	for (int i = 0; i < N; ++i) {
		int idx = maxN[i];
		predictions.push_back(std::make_pair(labels_[idx], output[idx]));
	}

	return predictions;
}

/* Load the mean file in binaryproto format. */
void MyClass::SetMean(const string& mean_file) {
	BlobProto blob_proto;
	ReadProtoFromBinaryFileOrDie(mean_file.c_str(), &blob_proto);

	/* Convert from BlobProto to Blob<float> */
	Blob<float> mean_blob;
	mean_blob.FromProto(blob_proto);
	CHECK_EQ(mean_blob.channels(), num_channels_)
			<< "Number of channels of mean file doesn't match input layer.";

	/* The format of the mean file is planar 32-bit float BGR or grayscale. */
	std::vector<cv::Mat> channels;
#ifdef CPU_ONLY
	float* data = mean_blob.mutable_cpu_data();
#else
	float* data = mean_blob.mutable_gpu_data();
#endif
	for (int i = 0; i < num_channels_; ++i) {
		/* Extract an individual channel. */
		cv::Mat channel(mean_blob.height(), mean_blob.width(), CV_32FC1, data);
		channels.push_back(channel);
		data += mean_blob.height() * mean_blob.width();
	}

	/* Merge the separate channels into a single image. */
	cv::Mat mean;
	cv::merge(channels, mean);

	/* Compute the global mean pixel value and create a mean image
	 * filled with this value. */
	cv::Scalar channel_mean = cv::mean(mean);
	mean_ = cv::Mat(input_geometry_, mean.type(), channel_mean);

}

std::vector<float> MyClass::Predict(const cv::Mat& img,
		caerFrameEvent single_frame) {

	Blob<float>* input_layer = net_->input_blobs()[0];
	input_layer->Reshape(1, num_channels_, input_geometry_.height,
			input_geometry_.width);
	/* Forward dimension change to all layers. */
	net_->Reshape();

	std::vector<cv::Mat> input_channels;
	WrapInputLayer(&input_channels);

	Preprocess(img, &input_channels);
	net_->ForwardPrefilled(); //Prefilled();

	//IF WE ENABLE VISUALIZATION IN REAL TIME
	if (0) {
		const vector<shared_ptr<Layer<float> > >& layers = net_->layers();
		int num_layer_conv = 0; // for graphics

		//image vector containing all layer activations
		vector < vector<cv::Mat> > layersVector;

		std::vector<int> ntot, ctot, htot, wtot, n_image_per_layer;

		// we want all activations of all layers
		for (int i = 0; i < layers.size(); i++) {
			// layer name
			//string layer_name = net_->layer_names()[i];
			//std::cout << "working on layer ID: " << i << " of type: "
			//		<< layers[i]->type() << " Name: " << layer_name
			//		<< std::endl;

			// layer blobs
			//const vector<Blob<float>*> & bottom_vecs = net_->bottom_vecs()[i];
			//const vector<Blob<float>*> & top_vecs = net_->top_vecs()[i];
			//layers[i]->Reshape(bottom_vecs, top_vecs);
			//layers[i]->Forward(bottom_vecs, top_vecs);

			// net blobs
			const vector < shared_ptr<Blob<float>>>&this_layer_blobs =
					net_->blobs();

			int n, c, h, w;
			float data;

			// get only convolutions
			if (strcmp(layers[i]->type(), "Convolution") == 0) {
				for (int j = 0; j < this_layer_blobs.size(); j++) {
					n = this_layer_blobs[j]->num();
					c = this_layer_blobs[j]->channels();
					h = this_layer_blobs[j]->height();
					w = this_layer_blobs[j]->width();

					// new image Vector For all Activations of this Layer
					std::vector<cv::Mat> imageVector;
					num_layer_conv += 1;

					//go over all channels/filters/activations
					//std::cout << "layers[i]->type() " << layers[i]->type()
					//		<< std::endl;
					//std::cout << "N " << n << std::endl;
					//std::cout << "C " << c << std::endl;
					//std::cout << "H " << h << std::endl;
					//std::cout << "W " << w << std::endl;
					ntot.push_back(n);
					ctot.push_back(c);
					htot.push_back(h);
					wtot.push_back(w);
					n_image_per_layer.push_back(n * c);
					for (int num = 0; num < n; num++) {
						//go over all channels
						for (int chan_num = 0; chan_num < c; chan_num++) {
							//go over h,w produce image
							cv::Mat newImage = cv::Mat::zeros(h, w, CV_32F);
							for (int hh = 0; hh < h; hh++) {
								//go over w
								for (int ww = 0; ww < w; ww++) {
									data = this_layer_blobs[j]->data_at(num,
											chan_num, hh, ww);
									newImage.at<float>(hh, ww) = data;
								}
							}
							imageVector.push_back(newImage);
						}
					}
					layersVector.push_back(imageVector);
				}
			}
		}


		if (num_layer_conv > 0) {
			//do the graphics only plot convolutional layers
			//divide the y in equal parts , one row per layer
			//single_frame->pixels[0] = (uint16_t) (20);
			//int y_pixels_per_layer = floor(
			//		single_frame->lengthY / num_layer_conv);
			//int x_pixels_per_layer = floor(single_frame->lengthX);
			int counter_y=-1,counter_x=-1;

			//std::cout << " SIZE " << layersVector.size() << std::endl;
			//loop over all conv layer
			int cs = 0;
			for (int layer_num = 0; layer_num < layersVector.size();
					layer_num++) {
                //std::cout << "num_layer_conv " << num_layer_conv << std::endl;
				//std::cout << "Layers Vector size " << layersVector.size() << std::endl;


				counter_y +=1; // count y position of image (layers)

				// loop over all in/out filters for this layer
				for (int img_num = 0; img_num < layersVector[layer_num].size();
						img_num++) {

					//std::cout << " layersVector[layer_num] " <<  layersVector[layer_num].size() << std::endl;

					counter_x +=1; // count number of images on x (filters)

					int size_x_single_image = floor(
							floor(single_frame->lengthY)
									/ layersVector[layer_num].size());
					int size_y_single_image = floor(single_frame->lengthX);
					cv::Size size(size_x_single_image, size_y_single_image);
					cv::Mat rescaled; //rescaled image

					// square size of each image in this layer
					//std::cout << " this image "
					//		<< layersVector[layer_num][img_num]<< std::endl;

					cv::resize(layersVector[layer_num][img_num], rescaled,
							size); //resize image

					//char buffer[255];
					//snprintf(buffer, sizeof(char) * 255, "/Users/federicocorradi/tmp/fileLayer%iImgNum%i.jpg", layer_num, img_num);
					//imwrite( buffer, rescaled*255);

					//now place images in the right place of the frame
					//rescaled = cv::norm(rescaled);
					//rescaled.convertTo(rescaled, CV_8U, 1/256);
					//std::cout << counter_y << std::endl;
					for(int x=0; x < size_x_single_image; x++){
						for(int y=0; y < size_y_single_image; y++){
							//round(rescaled.at<float>(x,y))
							//round(rescaled.at<float>(x,y)*65532))
							cs =  (y + (counter_y*size_y_single_image)) + (x * size_x_single_image)+(counter_x*size_x_single_image);
									//+ (y * size_y_single_image) + (counter_y*size_y_single_image);
									//+ x +(counter_x*size_x_single_image) ;
							single_frame->pixels[cs] = (uint16_t) ((int) (rescaled.at<float>(x,y)*255) << 8);
							single_frame->pixels[cs+1] = (uint16_t) ((int) (rescaled.at<float>(x,y)*255) << 8);
							single_frame->pixels[cs+2] = (uint16_t) ((int) (rescaled.at<float>(x,y)*255) << 8 );
							//std::cout << rescaled.at<float>(x,y) << std::endl;
							//cs += 3;
						}
					}

				}
			}
		}//num layer conv

	}//if 1

	/* Copy the output layer to a std::vector */
	Blob<float>* output_layer = net_->output_blobs()[0];

#ifdef CPU_ONLY
	const float* begin = output_layer->cpu_data();
#else
	const float* begin = output_layer->gpu_data();
#endif
	const float* end = begin + output_layer->channels();

	return std::vector<float>(begin, end);
}

/* Wrap the input layer of the network in separate cv::Mat objects
 * (one per channel). This way we save one memcpy operation and we
 * don't need to rely on cudaMemcpy2D. The last preprocessing
 * operation will write the separate channels directly to the input
 * layer. */
void MyClass::WrapInputLayer(std::vector<cv::Mat>* input_channels) {
	Blob<float>* input_layer = net_->input_blobs()[0];

	int width = input_layer->width();
	int height = input_layer->height();
	float* input_data = input_layer->mutable_cpu_data();
	for (int i = 0; i < input_layer->channels(); ++i) {
		cv::Mat channel(height, width, CV_32FC1, input_data);
		input_channels->push_back(channel);
		input_data += width * height;
	}
}

void MyClass::Preprocess(const cv::Mat& img,
		std::vector<cv::Mat>* input_channels) {
	/* Convert the input image to the input image format of the network. */

	// std::cout << " Preprocess --- img.channnels() " << img.channels() << ", num_channels_" << num_channels_ << std::endl;
	cv::Mat sample;
	if (img.channels() == 3 && num_channels_ == 1)
		cv::cvtColor(img, sample, cv::COLOR_BGR2GRAY);
	else if (img.channels() == 4 && num_channels_ == 1)
		cv::cvtColor(img, sample, cv::COLOR_BGRA2GRAY);
	else if (img.channels() == 4 && num_channels_ == 3)
		cv::cvtColor(img, sample, cv::COLOR_BGRA2BGR);
	else if (img.channels() == 1 && num_channels_ == 3)
		cv::cvtColor(img, sample, cv::COLOR_GRAY2BGR);
	else
		sample = img;

	cv::Mat sample_resized;
	if (sample.size() != input_geometry_)
		cv::resize(sample, sample_resized, input_geometry_);
	else
		sample_resized = sample;

	cv::Mat sample_float;
	if (num_channels_ == 3)
		sample_resized.convertTo(sample_float, CV_32FC3);
	else
		sample_resized.convertTo(sample_float, CV_32FC1);

	cv::Mat sample_normalized;
	mean_ = cv::Mat::zeros(1, 1, CV_64F); //TODO remove, compute mean_ from mean_file and adapt size for subtraction.
	//std::cout << " Preprocess: mean_size " << mean_.size() << std::endl;

	cv::subtract(sample_float, mean_, sample_normalized);

	/* This operation will write the separate BGR planes directly to the
	 * input layer of the network because it is wrapped by the cv::Mat
	 * objects in input_channels. */
	cv::split(sample_normalized, *input_channels);

	CHECK(reinterpret_cast<float*>(input_channels->at(0).data)
#ifdef CPU_ONLY
			== net_->input_blobs()[0]->cpu_data())
#else
			== net_->input_blobs()[0]->gpu_data())
#endif
			<< "Input channels are not wrapping the input layer of the network.";
}

