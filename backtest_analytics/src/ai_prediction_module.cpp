#include "../include/ai_prediction_module.hpp"
#include "../../shared/include/utils/logger.hpp"
#include <algorithm>
#include <numeric>
#include <cmath>
#include <random>
#include <fstream>
#include <sstream>

namespace ats {
namespace backtest {

// FeatureVector Implementation
std::vector<double> FeatureVector::to_flat_vector() const {
    std::vector<double> flat;
    
    flat.insert(flat.end(), price_features.begin(), price_features.end());
    flat.insert(flat.end(), volume_features.begin(), volume_features.end());
    flat.insert(flat.end(), spread_features.begin(), spread_features.end());
    flat.insert(flat.end(), volatility_features.begin(), volatility_features.end());
    flat.insert(flat.end(), technical_features.begin(), technical_features.end());
    
    return flat;
}

size_t FeatureVector::feature_count() const {
    return price_features.size() + volume_features.size() + 
           spread_features.size() + volatility_features.size() + 
           technical_features.size();
}

// FeatureEngineer Implementation
FeatureEngineer::FeatureEngineer() = default;
FeatureEngineer::~FeatureEngineer() = default;

FeatureVector FeatureEngineer::extract_features(const std::vector<MarketDataPoint>& historical_data,
                                               const MarketDataPoint& current_data) const {
    FeatureVector features;
    features.timestamp = current_data.timestamp;
    features.symbol = current_data.symbol;
    features.exchange = current_data.exchange;
    
    if (historical_data.empty()) {
        throw FeatureExtractionException("Historical data is empty");
    }
    
    try {
        // Extract price data
        std::vector<double> prices, opens, highs, lows, closes;
        for (const auto& data : historical_data) {
            prices.push_back(data.close_price);
            opens.push_back(data.open_price);
            highs.push_back(data.high_price);
            lows.push_back(data.low_price);
            closes.push_back(data.close_price);
        }
        
        // Current price features
        features.price_features.push_back(current_data.close_price);
        features.price_features.push_back(current_data.open_price);
        features.price_features.push_back(current_data.high_price);
        features.price_features.push_back(current_data.low_price);
        
        // Price returns
        if (prices.size() >= 2) {
            double return_1d = (prices.back() - prices[prices.size()-2]) / prices[prices.size()-2];
            features.price_features.push_back(return_1d);
        }
        
        // Moving averages
        auto ma_periods = std::vector<int>{5, 10, 20};
        auto moving_averages = calculate_moving_averages(prices, ma_periods);
        features.price_features.insert(features.price_features.end(), 
                                      moving_averages.begin(), moving_averages.end());
        
        // Technical indicators
        if (prices.size() >= 14) {
            auto rsi = calculate_rsi(prices);
            if (!rsi.empty()) {
                features.technical_features.push_back(rsi.back());
            }
        }
        
        if (prices.size() >= 20) {
            auto bollinger = calculate_bollinger_bands(prices);
            features.technical_features.insert(features.technical_features.end(),
                                              bollinger.begin(), bollinger.end());
        }
        
        if (prices.size() >= 26) {
            auto macd = calculate_macd(prices);
            features.technical_features.insert(features.technical_features.end(),
                                              macd.begin(), macd.end());
        }
        
        // Volume features
        features.volume_features.push_back(current_data.volume);
        auto volume_indicators = calculate_volume_indicators(historical_data);
        features.volume_features.insert(features.volume_features.end(),
                                       volume_indicators.begin(), volume_indicators.end());
        
        // Volatility features
        auto volatility_features = calculate_volatility_features(prices);
        features.volatility_features.insert(features.volatility_features.end(),
                                           volatility_features.begin(), volatility_features.end());
        
        // Normalize features
        auto flat_features = features.to_flat_vector();
        auto normalized = normalize_features(flat_features);
        
        // Reconstruct normalized feature vector
        size_t idx = 0;
        for (size_t i = 0; i < features.price_features.size(); ++i) {
            features.price_features[i] = normalized[idx++];
        }
        for (size_t i = 0; i < features.volume_features.size(); ++i) {
            features.volume_features[i] = normalized[idx++];
        }
        for (size_t i = 0; i < features.spread_features.size(); ++i) {
            features.spread_features[i] = normalized[idx++];
        }
        for (size_t i = 0; i < features.volatility_features.size(); ++i) {
            features.volatility_features[i] = normalized[idx++];
        }
        for (size_t i = 0; i < features.technical_features.size(); ++i) {
            features.technical_features[i] = normalized[idx++];
        }
        
    } catch (const std::exception& e) {
        throw FeatureExtractionException("Failed to extract features: " + std::string(e.what()));
    }
    
    return features;
}

std::vector<double> FeatureEngineer::calculate_moving_averages(const std::vector<double>& prices,
                                                              const std::vector<int>& periods) const {
    std::vector<double> mas;
    
    for (int period : periods) {
        if (static_cast<int>(prices.size()) >= period) {
            double sum = 0.0;
            for (int i = prices.size() - period; i < static_cast<int>(prices.size()); ++i) {
                sum += prices[i];
            }
            mas.push_back(sum / period);
        } else {
            mas.push_back(prices.back()); // Use current price if insufficient data
        }
    }
    
    return mas;
}

std::vector<double> FeatureEngineer::calculate_rsi(const std::vector<double>& prices, int period) const {
    std::vector<double> rsi_values;
    
    if (static_cast<int>(prices.size()) < period + 1) {
        return rsi_values;
    }
    
    std::vector<double> gains, losses;
    
    // Calculate price changes
    for (size_t i = 1; i < prices.size(); ++i) {
        double change = prices[i] - prices[i-1];
        gains.push_back(change > 0 ? change : 0.0);
        losses.push_back(change < 0 ? -change : 0.0);
    }
    
    // Calculate initial average gain and loss
    double avg_gain = 0.0, avg_loss = 0.0;
    for (int i = 0; i < period; ++i) {
        avg_gain += gains[i];
        avg_loss += losses[i];
    }
    avg_gain /= period;
    avg_loss /= period;
    
    // Calculate RSI
    for (size_t i = period; i < gains.size(); ++i) {
        avg_gain = (avg_gain * (period - 1) + gains[i]) / period;
        avg_loss = (avg_loss * (period - 1) + losses[i]) / period;
        
        if (avg_loss == 0.0) {
            rsi_values.push_back(100.0);
        } else {
            double rs = avg_gain / avg_loss;
            double rsi = 100.0 - (100.0 / (1.0 + rs));
            rsi_values.push_back(rsi);
        }
    }
    
    return rsi_values;
}

std::vector<double> FeatureEngineer::calculate_bollinger_bands(const std::vector<double>& prices,
                                                              int period, double std_dev) const {
    std::vector<double> bb_values;
    
    if (static_cast<int>(prices.size()) < period) {
        return bb_values;
    }
    
    // Calculate moving average
    double sum = 0.0;
    for (int i = prices.size() - period; i < static_cast<int>(prices.size()); ++i) {
        sum += prices[i];
    }
    double ma = sum / period;
    
    // Calculate standard deviation
    double variance = 0.0;
    for (int i = prices.size() - period; i < static_cast<int>(prices.size()); ++i) {
        variance += std::pow(prices[i] - ma, 2);
    }
    double stddev = std::sqrt(variance / period);
    
    // Calculate bands
    double upper_band = ma + (std_dev * stddev);
    double lower_band = ma - (std_dev * stddev);
    
    bb_values.push_back(upper_band);
    bb_values.push_back(ma);
    bb_values.push_back(lower_band);
    
    // Current price position relative to bands
    double current_price = prices.back();
    double bb_position = (current_price - lower_band) / (upper_band - lower_band);
    bb_values.push_back(bb_position);
    
    return bb_values;
}

std::vector<double> FeatureEngineer::calculate_macd(const std::vector<double>& prices,
                                                   int fast_period, int slow_period, int signal_period) const {
    std::vector<double> macd_values;
    
    if (static_cast<int>(prices.size()) < slow_period) {
        return macd_values;
    }
    
    // Calculate EMAs
    auto fast_ema = apply_exponential_smoothing(prices, 2.0 / (fast_period + 1));
    auto slow_ema = apply_exponential_smoothing(prices, 2.0 / (slow_period + 1));
    
    if (fast_ema.empty() || slow_ema.empty()) {
        return macd_values;
    }
    
    // MACD line
    double macd_line = fast_ema.back() - slow_ema.back();
    macd_values.push_back(macd_line);
    
    // For simplicity, just return MACD line
    // In full implementation, would calculate signal line and histogram
    
    return macd_values;
}

std::vector<double> FeatureEngineer::calculate_volume_indicators(const std::vector<MarketDataPoint>& data) const {
    std::vector<double> volume_features;
    
    if (data.empty()) {
        return volume_features;
    }
    
    // Current volume
    volume_features.push_back(data.back().volume);
    
    // Volume moving average
    if (data.size() >= 10) {
        double volume_sum = 0.0;
        for (size_t i = data.size() - 10; i < data.size(); ++i) {
            volume_sum += data[i].volume;
        }
        volume_features.push_back(volume_sum / 10.0);
    }
    
    // Volume weighted average price
    double vwap = calculate_volume_weighted_price(data);
    volume_features.push_back(vwap);
    
    return volume_features;
}

double FeatureEngineer::calculate_volume_weighted_price(const std::vector<MarketDataPoint>& data) const {
    if (data.empty()) {
        return 0.0;
    }
    
    double total_volume = 0.0;
    double total_volume_price = 0.0;
    
    for (const auto& point : data) {
        total_volume += point.volume;
        total_volume_price += point.close_price * point.volume;
    }
    
    return total_volume > 0.0 ? total_volume_price / total_volume : data.back().close_price;
}

std::vector<double> FeatureEngineer::calculate_volatility_features(const std::vector<double>& prices) const {
    std::vector<double> vol_features;
    
    if (prices.size() < 2) {
        return vol_features;
    }
    
    // Calculate returns
    std::vector<double> returns;
    for (size_t i = 1; i < prices.size(); ++i) {
        returns.push_back((prices[i] - prices[i-1]) / prices[i-1]);
    }
    
    // Standard deviation of returns
    double volatility = calculate_standard_deviation(returns);
    vol_features.push_back(volatility);
    
    // Historical volatility (annualized)
    double annualized_vol = volatility * std::sqrt(252); // Assuming daily data
    vol_features.push_back(annualized_vol);
    
    return vol_features;
}

std::vector<double> FeatureEngineer::normalize_features(const std::vector<double>& features) const {
    std::vector<double> normalized;
    
    for (size_t i = 0; i < features.size(); ++i) {
        // Simple z-score normalization
        // In production, would use running statistics
        double mean = 0.0;
        double std_dev = 1.0;
        
        double normalized_value = (features[i] - mean) / std_dev;
        normalized.push_back(normalized_value);
    }
    
    return normalized;
}

double FeatureEngineer::calculate_standard_deviation(const std::vector<double>& values) const {
    if (values.size() < 2) {
        return 0.0;
    }
    
    double mean = std::accumulate(values.begin(), values.end(), 0.0) / values.size();
    double variance = 0.0;
    
    for (double value : values) {
        variance += std::pow(value - mean, 2);
    }
    
    return std::sqrt(variance / (values.size() - 1));
}

std::vector<double> FeatureEngineer::apply_exponential_smoothing(const std::vector<double>& data, double alpha) const {
    std::vector<double> smoothed;
    
    if (data.empty()) {
        return smoothed;
    }
    
    smoothed.push_back(data[0]);
    
    for (size_t i = 1; i < data.size(); ++i) {
        double smoothed_value = alpha * data[i] + (1.0 - alpha) * smoothed.back();
        smoothed.push_back(smoothed_value);
    }
    
    return smoothed;
}

// LinearRegressionModel Implementation
LinearRegressionModel::LinearRegressionModel() = default;

bool LinearRegressionModel::train(const std::vector<FeatureVector>& training_data,
                                 const std::vector<double>& target_values) {
    if (training_data.empty() || target_values.empty() || 
        training_data.size() != target_values.size()) {
        return false;
    }
    
    try {
        // Convert feature vectors to matrix
        std::vector<std::vector<double>> X;
        for (const auto& features : training_data) {
            auto flat_features = features.to_flat_vector();
            flat_features.push_back(1.0); // Add bias term
            X.push_back(flat_features);
        }
        
        if (X.empty()) {
            return false;
        }
        
        feature_count_ = X[0].size() - 1; // Exclude bias term
        
        // Normal equation: theta = (X^T * X)^(-1) * X^T * y
        auto X_transpose = matrix_transpose(X);
        auto XtX = matrix_multiply(X_transpose, X);
        auto XtX_inv = matrix_inverse(XtX);
        auto Xty = matrix_vector_multiply(X_transpose, target_values);
        auto theta = matrix_vector_multiply(XtX_inv, Xty);
        
        // Extract weights and bias
        weights_.clear();
        for (size_t i = 0; i < theta.size() - 1; ++i) {
            weights_.push_back(theta[i]);
        }
        bias_ = theta.back();
        
        // Calculate training MSE
        double mse = 0.0;
        for (size_t i = 0; i < training_data.size(); ++i) {
            auto pred = predict(training_data[i]);
            double error = pred.spread_prediction - target_values[i];
            mse += error * error;
        }
        training_mse_ = mse / training_data.size();
        
        is_trained_ = true;
        LOG_INFO("Linear regression model trained with {} samples, MSE: {:.6f}", 
                 training_data.size(), training_mse_);
        
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Linear regression training failed: {}", e.what());
        return false;
    }
}

PredictionResult LinearRegressionModel::predict(const FeatureVector& features) {
    PredictionResult result;
    result.model_version = model_version_;
    result.prediction_time = std::chrono::system_clock::now();
    result.symbol = features.symbol;
    
    if (!is_trained_ || weights_.empty()) {
        result.confidence_score = 0.0;
        return result;
    }
    
    try {
        auto flat_features = features.to_flat_vector();
        
        if (flat_features.size() != feature_count_) {
            LOG_WARNING("Feature size mismatch: expected {}, got {}", 
                       feature_count_, flat_features.size());
            result.confidence_score = 0.0;
            return result;
        }
        
        // Linear prediction: y = w * x + b
        double prediction = bias_;
        for (size_t i = 0; i < weights_.size() && i < flat_features.size(); ++i) {
            prediction += weights_[i] * flat_features[i];
        }
        
        result.spread_prediction = prediction;
        
        // Simple confidence based on training MSE
        result.confidence_score = std::max(0.0, 1.0 - training_mse_);
        result.confidence_score = std::min(1.0, result.confidence_score);
        
        // Risk assessment based on prediction magnitude
        result.risk_score = std::abs(prediction) / 0.05; // Normalized to expected spread range
        result.risk_score = std::min(1.0, result.risk_score);
        
        if (result.risk_score < 0.3) {
            result.risk_category = "low";
        } else if (result.risk_score < 0.7) {
            result.risk_category = "medium";
        } else {
            result.risk_category = "high";
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("Prediction failed: {}", e.what());
        result.confidence_score = 0.0;
    }
    
    return result;
}

double LinearRegressionModel::evaluate(const std::vector<FeatureVector>& test_data,
                                      const std::vector<double>& test_targets) {
    if (!is_trained_ || test_data.empty() || test_targets.empty() ||
        test_data.size() != test_targets.size()) {
        return 0.0;
    }
    
    double mse = 0.0;
    for (size_t i = 0; i < test_data.size(); ++i) {
        auto pred = predict(test_data[i]);
        double error = pred.spread_prediction - test_targets[i];
        mse += error * error;
    }
    
    return mse / test_data.size();
}

bool LinearRegressionModel::save_model(const std::string& file_path) {
    if (!is_trained_) {
        return false;
    }
    
    try {
        std::ofstream file(file_path);
        if (!file.is_open()) {
            return false;
        }
        
        file << "LinearRegressionModel\n";
        file << model_version_ << "\n";
        file << feature_count_ << "\n";
        file << bias_ << "\n";
        file << training_mse_ << "\n";
        
        for (double weight : weights_) {
            file << weight << " ";
        }
        file << "\n";
        
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to save model: {}", e.what());
        return false;
    }
}

bool LinearRegressionModel::load_model(const std::string& file_path) {
    try {
        std::ifstream file(file_path);
        if (!file.is_open()) {
            return false;
        }
        
        std::string model_type;
        std::getline(file, model_type);
        if (model_type != "LinearRegressionModel") {
            return false;
        }
        
        file >> model_version_;
        file >> feature_count_;
        file >> bias_;
        file >> training_mse_;
        
        weights_.clear();
        for (size_t i = 0; i < feature_count_; ++i) {
            double weight;
            file >> weight;
            weights_.push_back(weight);
        }
        
        is_trained_ = true;
        LOG_INFO("Linear regression model loaded from {}", file_path);
        
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to load model: {}", e.what());
        return false;
    }
}

// Matrix operations (simplified implementations)
std::vector<double> LinearRegressionModel::matrix_vector_multiply(
    const std::vector<std::vector<double>>& matrix,
    const std::vector<double>& vector) const {
    
    std::vector<double> result(matrix.size(), 0.0);
    
    for (size_t i = 0; i < matrix.size(); ++i) {
        for (size_t j = 0; j < matrix[i].size() && j < vector.size(); ++j) {
            result[i] += matrix[i][j] * vector[j];
        }
    }
    
    return result;
}

std::vector<std::vector<double>> LinearRegressionModel::matrix_transpose(
    const std::vector<std::vector<double>>& matrix) const {
    
    if (matrix.empty() || matrix[0].empty()) {
        return {};
    }
    
    std::vector<std::vector<double>> result(matrix[0].size(), std::vector<double>(matrix.size()));
    
    for (size_t i = 0; i < matrix.size(); ++i) {
        for (size_t j = 0; j < matrix[i].size(); ++j) {
            result[j][i] = matrix[i][j];
        }
    }
    
    return result;
}

std::vector<std::vector<double>> LinearRegressionModel::matrix_multiply(
    const std::vector<std::vector<double>>& a,
    const std::vector<std::vector<double>>& b) const {
    
    if (a.empty() || b.empty() || a[0].size() != b.size()) {
        return {};
    }
    
    std::vector<std::vector<double>> result(a.size(), std::vector<double>(b[0].size(), 0.0));
    
    for (size_t i = 0; i < a.size(); ++i) {
        for (size_t j = 0; j < b[0].size(); ++j) {
            for (size_t k = 0; k < b.size(); ++k) {
                result[i][j] += a[i][k] * b[k][j];
            }
        }
    }
    
    return result;
}

std::vector<std::vector<double>> LinearRegressionModel::matrix_inverse(
    const std::vector<std::vector<double>>& matrix) const {
    // Simplified implementation - in production would use proper matrix inversion
    // For now, return identity matrix scaled by 0.01 to avoid singularity
    
    size_t n = matrix.size();
    std::vector<std::vector<double>> result(n, std::vector<double>(n, 0.0));
    
    for (size_t i = 0; i < n; ++i) {
        result[i][i] = 0.01; // Regularization
    }
    
    return result;
}

// AIPredictionModule Implementation
AIPredictionModule::AIPredictionModule() = default;
AIPredictionModule::~AIPredictionModule() = default;

bool AIPredictionModule::initialize(const AIPredictionConfig& config) {
    config_ = config;
    
    try {
        feature_engineer_ = std::make_unique<FeatureEngineer>();
        model_ = create_model(config_.model_type);
        
        if (!model_) {
            LOG_ERROR("Failed to create model of type: {}", config_.model_type);
            return false;
        }
        
        LOG_INFO("AI Prediction Module initialized with model type: {}", config_.model_type);
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to initialize AI Prediction Module: {}", e.what());
        return false;
    }
}

std::unique_ptr<MLModel> AIPredictionModule::create_model(const std::string& model_type) {
    if (model_type == "linear_regression") {
        return std::make_unique<LinearRegressionModel>();
    } else if (model_type == "random_forest") {
        return std::make_unique<RandomForestModel>();
    } else {
        LOG_WARNING("Unknown model type: {}, defaulting to linear regression", model_type);
        return std::make_unique<LinearRegressionModel>();
    }
}

bool AIPredictionModule::train_model(const std::vector<MarketDataPoint>& historical_data) {
    if (!model_ || !feature_engineer_) {
        LOG_ERROR("Model or feature engineer not initialized");
        return false;
    }
    
    try {
        // Prepare training data
        training_features_.clear();
        training_targets_.clear();
        
        // Extract features from historical data windows
        for (size_t i = config_.price_window_size; i < historical_data.size(); ++i) {
            std::vector<MarketDataPoint> window(
                historical_data.begin() + i - config_.price_window_size,
                historical_data.begin() + i);
            
            auto features = feature_engineer_->extract_features(window, historical_data[i]);
            training_features_.push_back(features);
            
            // Simple target: next period price change
            if (i + 1 < historical_data.size()) {
                double target = (historical_data[i+1].close_price - historical_data[i].close_price) 
                              / historical_data[i].close_price;
                training_targets_.push_back(target);
            }
        }
        
        if (training_features_.empty()) {
            LOG_ERROR("No training features extracted");
            return false;
        }
        
        // Train the model
        bool success = model_->train(training_features_, training_targets_);
        if (success) {
            last_training_time_ = std::chrono::system_clock::now();
            LOG_INFO("Model trained with {} samples", training_features_.size());
        }
        
        return success;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Model training failed: {}", e.what());
        return false;
    }
}

PredictionResult AIPredictionModule::predict_spread(const std::vector<MarketDataPoint>& recent_data,
                                                   const std::string& symbol,
                                                   const std::string& exchange1,
                                                   const std::string& exchange2) {
    PredictionResult result;
    result.symbol = symbol;
    result.prediction_time = std::chrono::system_clock::now();
    
    if (!is_model_ready()) {
        result.confidence_score = 0.0;
        return result;
    }
    
    try {
        // Check cache first
        std::string cache_key = generate_cache_key(symbol, exchange1, exchange2);
        auto cache_it = prediction_cache_.find(cache_key);
        
        auto now = std::chrono::system_clock::now();
        auto cache_age = std::chrono::duration_cast<std::chrono::seconds>(
            now - last_prediction_update_).count();
        
        if (cache_it != prediction_cache_.end() && 
            cache_age < config_.update_frequency_seconds) {
            return cache_it->second;
        }
        
        // Filter data for the specific symbol
        std::vector<MarketDataPoint> symbol_data;
        for (const auto& data : recent_data) {
            if (data.symbol == symbol) {
                symbol_data.push_back(data);
            }
        }
        
        if (symbol_data.size() < static_cast<size_t>(config_.price_window_size)) {
            result.confidence_score = 0.0;
            return result;
        }
        
        // Extract features
        std::vector<MarketDataPoint> window(
            symbol_data.end() - config_.price_window_size,
            symbol_data.end());
        
        auto features = feature_engineer_->extract_features(window, symbol_data.back());
        
        // Make prediction
        result = model_->predict(features);
        result.symbol = symbol;
        result.prediction_time = now;
        result.target_time = now + std::chrono::minutes(config_.prediction_horizon_minutes);
        
        // Cache the result
        prediction_cache_[cache_key] = result;
        last_prediction_update_ = now;
        
        // Validate prediction
        if (!is_prediction_valid(result)) {
            result.confidence_score = 0.0;
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("Prediction failed for {}: {}", symbol, e.what());
        result.confidence_score = 0.0;
    }
    
    return result;
}

bool AIPredictionModule::is_model_ready() const {
    return model_ && model_->get_feature_count() > 0 && !training_features_.empty();
}

std::string AIPredictionModule::generate_cache_key(const std::string& symbol, 
                                                  const std::string& exchange1,
                                                  const std::string& exchange2) const {
    return symbol + "_" + exchange1 + "_" + exchange2;
}

bool AIPredictionModule::is_prediction_valid(const PredictionResult& prediction) const {
    if (prediction.confidence_score < config_.confidence_threshold) {
        return false;
    }
    
    auto age = std::chrono::duration_cast<std::chrono::minutes>(
        std::chrono::system_clock::now() - prediction.prediction_time).count();
    
    return age <= config_.max_prediction_age_minutes;
}

} // namespace backtest
} // namespace ats