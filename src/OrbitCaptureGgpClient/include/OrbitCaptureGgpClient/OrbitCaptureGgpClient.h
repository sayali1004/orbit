// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ORBIT_CAPTURE_GGP_CLIENT_ORBIT_CAPTURE_GGP_CLIENT_H_
#define ORBIT_CAPTURE_GGP_CLIENT_ORBIT_CAPTURE_GGP_CLIENT_H_

#include <memory>
#include <string>
#include <string_view>
#include <vector>

class CaptureClientGgpClient {
 public:
  explicit CaptureClientGgpClient(std::string_view grpc_server_address);
  ~CaptureClientGgpClient();
  CaptureClientGgpClient(CaptureClientGgpClient&&);
  CaptureClientGgpClient& operator=(CaptureClientGgpClient&&);

  int StartCapture();
  int StopCapture();
  int UpdateSelectedFunctions(const std::vector<std::string>& selected_functions);
  void ShutdownService();

 private:
  class CaptureClientGgpClientImpl;
  std::unique_ptr<CaptureClientGgpClientImpl> pimpl;
};

#endif  // ORBIT_CAPTURE_GGP_CLIENT_ORBIT_CAPTURE_GGP_CLIENT_H_