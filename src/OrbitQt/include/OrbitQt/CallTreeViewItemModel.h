// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ORBIT_QT_CALL_TREE_VIEW_ITEM_MODEL_H_
#define ORBIT_QT_CALL_TREE_VIEW_ITEM_MODEL_H_

#include <QAbstractItemModel>
#include <QMetaType>
#include <QModelIndex>
#include <QObject>
#include <QString>
#include <QVariant>
#include <Qt>
#include <memory>
#include <vector>

#include "ClientData/CallstackEvent.h"
#include "OrbitGl/CallTreeView.h"

Q_DECLARE_METATYPE(const std::vector<orbit_client_data::CallstackEvent>*)

class CallTreeViewItemModel : public QAbstractItemModel {
  Q_OBJECT

 public:
  explicit CallTreeViewItemModel(std::unique_ptr<CallTreeView> call_tree_view,
                                 QObject* parent = nullptr);

  QVariant data(const QModelIndex& index, int role) const override;
  Qt::ItemFlags flags(const QModelIndex& index) const override;
  QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
  QModelIndex index(int row, int column, const QModelIndex& parent) const override;
  QModelIndex parent(const QModelIndex& index) const override;
  int rowCount(const QModelIndex& parent) const override;
  int columnCount(const QModelIndex& parent) const override;

  enum Columns {
    kThreadOrFunction = 0,
    kInclusive,
    kExclusive,
    kOfParent,
    kModule,
    kFunctionAddress,
    kColumnCount
  };

  static const int kMatchesCustomFilterRole = Qt::UserRole;
  static const int kModulePathRole = Qt::UserRole + 1;
  static const int kModuleBuildIdRole = Qt::UserRole + 2;
  static const int kCopyableValueRole = Qt::UserRole + 3;
  static const int kExclusiveCallstackEventsRole = Qt::UserRole + 4;

 private:
  [[nodiscard]] QVariant GetDisplayRoleData(const QModelIndex& index) const;
  [[nodiscard]] QVariant GetEditRoleData(const QModelIndex& index) const;
  [[nodiscard]] QVariant GetToolTipRoleData(const QModelIndex& index) const;
  [[nodiscard]] QVariant GetForegroundRoleData(const QModelIndex& index) const;
  [[nodiscard]] QVariant GetModulePathRoleData(const QModelIndex& index) const;
  [[nodiscard]] QVariant GetModuleBuildIdRoleData(const QModelIndex& index) const;
  [[nodiscard]] QVariant GetCopyableValueRoleData(const QModelIndex& index) const;
  [[nodiscard]] static QVariant GetExclusiveCallstackEventsRoleData(const QModelIndex& index);

  std::unique_ptr<CallTreeView> call_tree_view_;
};

#endif  // ORBIT_QT_CALL_TREE_VIEW_ITEM_MODEL_H_
