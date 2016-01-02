/*
Copyright (C) 2015 Ruslan Kabatsayev <b7.10110111@gmail.com>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "RegisterView.h"
#include <QMouseEvent>
#include <QLabel>
#include <QApplication>
#include <QMessageBox>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QVBoxLayout>
#include <QPlastiqueStyle>
#include <algorithm>
#include <QDebug>
#include <iostream>
#include "RegisterViewModelBase.h"

namespace ODbgRegisterView {

// TODO: Right click => select too
// TODO: Menu key => open menu
// TODO: Enter key => modify/toggle
// TODO: Tooltips support
// TODO: GPR menu: Increment, Decrement, Invert, Zero(if not already), Set to 1(if not already)
// TODO: rFLAGS menu: Set Condition (O,P,NAE etc. - see ODB)
// TODO: FPU tags: toggle - set valid/empty
// TODO: FSR: Set Condition: L,E,Unordered
// TODO: PC: set 24/53/64-bit mantissa
// TODO: RC: round up/down/nearest

namespace
{

static const int MODEL_NAME_COLUMN=RegisterViewModelBase::Model::NAME_COLUMN;
static const int MODEL_VALUE_COLUMN=RegisterViewModelBase::Model::VALUE_COLUMN;
static const int MODEL_COMMENT_COLUMN=RegisterViewModelBase::Model::COMMENT_COLUMN;

template<typename T>
inline T sqr(T v) { return v*v; }

// Square of Euclidean distance between two widgets
inline int distSqr(QWidget const* w1, QWidget const* w2)
{
    return sqr(w1->x()-w2->x())+sqr(w1->y()-w2->y());
}

inline QSize letterSize(QFont const& font)
{
    const QFontMetrics fontMetrics(font);
    const int width=fontMetrics.width('w');
    const int height=fontMetrics.height();
    return QSize(width,height);
}

static QPlastiqueStyle plastiqueStyle;

}

// --------------------- FieldWidget impl ----------------------------------
QString FieldWidget::text() const
{
	if(!index.isValid())
		return QLabel::text();
	const auto text=index.data();
	if(!text.isValid())
		return "fW???";
	return text.toString();
}

void FieldWidget::init(int const fieldWidth)
{
	const auto charSize=letterSize(font());
	setFixedHeight(charSize.height());
	if(fieldWidth>0)
		setFixedWidth(fieldWidth*charSize.width());
	setDisabled(true);
}

FieldWidget::FieldWidget(int const fieldWidth, QModelIndex const& index, QWidget* const parent)
	: QLabel("Fw???",parent),
	  index(index)
{
	init(fieldWidth);
}

FieldWidget::FieldWidget(int const fieldWidth, QString const& fixedText, QWidget* const parent)
	: QLabel(fixedText,parent)
{
	init(fieldWidth); // NOTE: fieldWidth!=fixedText.length() in general
}

void FieldWidget::update()
{
	QLabel::setText(text());
	adjustSize();
}

// --------------------- ValueField impl ----------------------------------
ValueField::ValueField(int const fieldWidth,
					   QModelIndex const& index,
					   QWidget* const parent,
					   std::function<QString(QString const&)> const& valueFormatter
					  )
	: FieldWidget(fieldWidth,index,parent),
	  valueFormatter(valueFormatter)
{
	setDisabled(false);
	setMouseTracking(true);
	// Set some known style to avoid e.g. Oxygen's label transition animations, which
	// break updating of colors such as "register changed" when single-stepping frequently
	setStyle(&plastiqueStyle);
}

QString ValueField::text() const
{
	return valueFormatter(FieldWidget::text());
}

bool ValueField::changed() const
{
	Q_ASSERT(index.isValid());
	const auto changed=index.data(RegisterViewModelBase::Model::RegisterChangedRole);
	Q_ASSERT(changed.isValid());
	return changed.toBool();
}

QColor ValueField::fgColorForChangedField() const
{
	return Qt::red; // TODO: read from user palette
}

void ValueField::setNeighbors(ValueField* up,ValueField* down,ValueField* left,ValueField* right)
{
	up_=up;
	down_=down;
	left_=left;
	right_=right;
}

bool ValueField::isSelected() const
{
	return selected_;
}

void ValueField::update()
{
	FieldWidget::update();
	updatePalette();
}

void ValueField::updatePalette()
{
	auto palette=this->palette();
	const auto appPalette=QApplication::palette();

	const QColor selectedFGColor=appPalette.color(QPalette::HighlightedText);
	const QColor normalFGColor=appPalette.color(QPalette::WindowText);
	const QColor hoveredFGColor=normalFGColor;
	const QColor changedFGColor=fgColorForChangedField();

	palette.setColor(foregroundRole(),changed()  ? changedFGColor  : 
									  selected_ ? selectedFGColor :
									  hovered_  ? hoveredFGColor  : normalFGColor);

	setPalette(palette);
	QLabel::update();
}

void ValueField::enterEvent(QEvent*)
{
	hovered_=true;
	updatePalette();
}

void ValueField::leaveEvent(QEvent*)
{
	hovered_=false;
	updatePalette();
}

void ValueField::select()
{
	selected_=true;
	Q_EMIT selected();
	updatePalette();
}

void ValueField::mousePressEvent(QMouseEvent* event)
{
	if(event->button()==Qt::LeftButton)
		select();
}

void ValueField::unselect()
{
	selected_=false;
	updatePalette();
}

void ValueField::mouseDoubleClickEvent(QMouseEvent* event)
{
	mousePressEvent(event);
	QMessageBox::information(this,"Double-clicked",QString("Double-clicked field %1 with contents \"%2\"")
								.arg(QString().sprintf("%p",static_cast<void*>(this))).arg(text()));
}

void ValueField::paintEvent(QPaintEvent*)
{
	QPainter painter(this);
	QStyleOptionViewItemV4 option;
	option.rect=rect();
	option.showDecorationSelected=true;
	option.text=text();
	option.font=font();
	option.palette=palette();
	option.textElideMode=Qt::ElideNone;
	option.state |= QStyle::State_Enabled;
	if(selected_) option.state |= QStyle::State_Selected;
	if(hovered_)  option.state |= QStyle::State_MouseOver;
	if(palette().currentColorGroup()==QPalette::Active)
		option.state |= QStyle::State_Active;
	QApplication::style()->drawControl(QStyle::CE_ItemViewItem, &option, &painter);
}

// -------------------------------- RegisterGroup impl ----------------------------
RegisterGroup::RegisterGroup(QWidget* parent)
	: QWidget(parent)
{
}

void RegisterGroup::insert(int const line, int const column, FieldWidget* const widget)
{
	widget->update();
	if(auto* const value=dynamic_cast<ValueField*>(widget))
		connect(value,SIGNAL(selected()),this,SLOT(fieldSelected()));

	const auto charSize=letterSize(font());
	const auto charWidth=charSize.width();
	const auto charHeight=charSize.height();
	// extra space for highlighting rectangle, so that single-digit fields are easier to target
	const auto marginLeft=charWidth/2;
	const auto marginRight=charWidth-marginLeft;

	QPoint position(charWidth*column,charHeight*line);
	position -= QPoint(marginLeft,0);

	QSize size(widget->size());
	size += QSize(marginLeft+marginRight,0);

	widget->setMinimumSize(size);
	widget->move(position);
	// FIXME: why are e.g. regnames like FSR truncated without the -1?
	widget->setContentsMargins(marginLeft,0,marginRight-1,0);

	const auto potentialNewWidth=widget->pos().x()+widget->width();
	const auto potentialNewHeight=widget->pos().y()+widget->height();
	const auto oldMinSize=minimumSize();
	if(potentialNewWidth > oldMinSize.width() || potentialNewHeight > oldMinSize.height())
		setMinimumSize(std::max(potentialNewWidth,oldMinSize.width()),std::max(potentialNewHeight,oldMinSize.height()));

	widget->show();
}

int RegisterGroup::lineAfterLastField() const
{
	const auto fields=this->fields();
	const auto bottomField=std::max_element(fields.begin(),fields.end(),
												[](FieldWidget* l,FieldWidget* r)
												{ return l->pos().y()<r->pos().y(); });
	return bottomField==fields.end() ? 0 : (*bottomField)->pos().y()/(*bottomField)->height()+1;
}

void RegisterGroup::appendNameValueComment(QModelIndex const& nameIndex, bool insertComment)
{
	Q_ASSERT(nameIndex.isValid());
	using namespace RegisterViewModelBase;
	const auto nameWidth=nameIndex.data(Model::FixedLengthRole).toInt();
	Q_ASSERT(nameWidth>0);
	const auto valueIndex=nameIndex.sibling(nameIndex.row(),Model::VALUE_COLUMN);
	const auto valueWidth=valueIndex.data(Model::FixedLengthRole).toInt();
	Q_ASSERT(valueWidth>0);

	const int line=lineAfterLastField();
	int column=0;
	insert(line,column,new FieldWidget(nameWidth,nameIndex.data().toString(),this));
	column+=nameWidth+1;
	insert(line,column,new ValueField(valueWidth,valueIndex,this));
	if(insertComment)
	{
		column+=valueWidth+1;
		const auto commentIndex=nameIndex.sibling(nameIndex.row(),Model::COMMENT_COLUMN);
		insert(line,column,new FieldWidget(0,commentIndex,this));
	}
}

QList<FieldWidget*> RegisterGroup::fields() const
{
	const auto children=this->children();
	QList<FieldWidget*> fields;
	for(auto* const child : children)
	{
		const auto field=dynamic_cast<FieldWidget*>(child);
		if(field) fields.append(field);
	}
	return fields;
}

QList<ValueField*> RegisterGroup::valueFields() const
{
	QList<ValueField*> allValues;
	for(auto* const field : fields())
	{
		auto* const value=dynamic_cast<ValueField*>(field);
		if(value) allValues.push_back(value);
	}
	return allValues;
}

void RegisterGroup::mousePressEvent(QMouseEvent* event)
{
	if(event->type()!=QEvent::MouseButtonPress) return;
	for(auto* const field : valueFields()) field->unselect();
}

void RegisterGroup::fieldSelected()
{
	for(auto* const field : valueFields())
		if(sender()!=field)
			field->unselect();
}

void RegisterGroup::adjustWidth()
{
	int widthNeeded=0;
	for(auto* const field : valueFields())
	{
		const auto widthToRequire=field->pos().x()+field->width();
		if(widthToRequire>widthNeeded) widthNeeded=widthToRequire;
	}
	setMinimumWidth(widthNeeded);
}

ODBRegView::ODBRegView(QWidget* parent)
    : QScrollArea(parent)
{
	QFont font("Monospace");
	font.setStyleHint(QFont::TypeWriter);
	setFont(font);

	auto* const canvas=new QWidget(this);
	auto* const canvasLayout=new QVBoxLayout(canvas);
	canvasLayout->setSpacing(letterSize(this->font()).height()/2);
	canvasLayout->setContentsMargins(contentsMargins());
	canvasLayout->setAlignment(Qt::AlignTop);
	canvas->setLayout(canvasLayout);
	canvas->setBackgroundRole(QPalette::Base);
	canvas->setAutoFillBackground(true);

    setWidget(canvas);
    setWidgetResizable(true);
	// TODO: make this list user-selectable
	regGroupTypes={RegisterGroupType::GPR,
				   RegisterGroupType::rIP,
				   RegisterGroupType::Segment,
				   RegisterGroupType::EFL};
}

void ODBRegView::setModel(QAbstractItemModel* model)
{
	model_=model;
	connect(model,SIGNAL(modelReset()),this,SLOT(modelReset()));
	connect(model,SIGNAL(dataChanged(QModelIndex const&,QModelIndex const&)),this,SLOT(modelUpdated()));
	modelReset();
}

// TODO: switch from string-based search to enum-based one (add a new Role to model data)
QModelIndex findModelCategory(QAbstractItemModel const*const model,
							  QString const& catToFind)
{
	for(int row=0;row<model->rowCount();++row)
	{
		const auto cat=model->index(row,0).data(MODEL_NAME_COLUMN);
		if(cat.isValid() && cat.toString()==catToFind)
			return model->index(row,0);
	}
	return QModelIndex();
}

// TODO: switch from string-based search to enum-based one (add a new Role to model data)
QModelIndex findModelRegister(QModelIndex categoryIndex,
							  QString const& regToFind)
{
	auto* const model=categoryIndex.model();
	for(int row=0;row<model->rowCount(categoryIndex);++row)
	{
		const auto regIndex=model->index(row,MODEL_NAME_COLUMN,categoryIndex);
		const auto name=model->data(regIndex).toString();
		if(name.toUpper()==regToFind)
			return regIndex;
	}
	return QModelIndex();
}

void ODBRegView::addGroup(RegisterGroupType type)
{
	if(!model_->rowCount()) return;
	std::vector<QModelIndex> nameValCommentIndices;
	switch(type)
	{
	case RegisterGroupType::GPR:
	{
		const auto catIndex=findModelCategory(model_,"General Purpose");
		if(!catIndex.isValid()) break;
		for(int row=0;row<model_->rowCount(catIndex);++row)
			nameValCommentIndices.emplace_back(model_->index(row,MODEL_NAME_COLUMN,catIndex));
		break;
	}
	case RegisterGroupType::Segment:
	{
		const auto catIndex=findModelCategory(model_,"Segment");
		if(!catIndex.isValid()) break;
		for(int row=0;row<model_->rowCount(catIndex);++row)
			nameValCommentIndices.emplace_back(model_->index(row,MODEL_NAME_COLUMN,catIndex));
		break;
	}
	case RegisterGroupType::rIP:
	{
		const auto catIndex=findModelCategory(model_,"General Status");
		if(!catIndex.isValid()) break;
		nameValCommentIndices.emplace_back(findModelRegister(catIndex,"RIP"));
		nameValCommentIndices.emplace_back(findModelRegister(catIndex,"EIP"));
		break;
	}
	case RegisterGroupType::EFL:
	{
		using RegisterViewModelBase::Model;
		const auto catIndex=findModelCategory(model_,"General Status");
		if(!catIndex.isValid()) break;
		auto nameIndex=findModelRegister(catIndex,"RFLAGS");
		if(!nameIndex.isValid())
			nameIndex=findModelRegister(catIndex,"EFLAGS");
		if(!nameIndex.isValid()) break;
		groups.push_back(new RegisterGroup(this));
		auto* const group=groups.back();
		const int nameWidth=3;
		int column=0;
		group->insert(0,column,new FieldWidget(nameWidth,"EFL",group));
		const auto valueWidth=8;
		const auto valueIndex=nameIndex.sibling(nameIndex.row(),Model::VALUE_COLUMN);
		column+=nameWidth+1;
		group->insert(0,column,new ValueField(valueWidth,valueIndex,group,[](QString const& v){return v.right(8);}));
		const auto commentIndex=nameIndex.sibling(nameIndex.row(),Model::COMMENT_COLUMN);
		column+=valueWidth+1;
		group->insert(0,column,new FieldWidget(0,commentIndex,group));
		static_cast<QVBoxLayout*>(widget()->layout())->addWidget(group);
		return;
	}
	default:
		qWarning() << "Warning: unexpected register group type requested in" << Q_FUNC_INFO;
		return;
	}
	nameValCommentIndices.erase(std::remove_if(nameValCommentIndices.begin(),
											   nameValCommentIndices.end(),
											   [](QModelIndex const& index){ return !index.isValid(); })
								,nameValCommentIndices.end());
	if(nameValCommentIndices.empty())
	{
		qWarning() << "Warning: failed to get any useful register indices for regGroupType" << static_cast<long>(type);
		return;
	}
	groups.push_back(new RegisterGroup(this));
	auto* const group=groups.back();
	for(const auto& index : nameValCommentIndices)
		group->appendNameValueComment(index);
	static_cast<QVBoxLayout*>(widget()->layout())->addWidget(group);
}

void ODBRegView::modelReset()
{
	setWidget(nullptr);
	for(auto* const group : groups)
		group->deleteLater();
	groups.clear();
	for(auto groupType : regGroupTypes)
		addGroup(groupType);
	finalize();
}

void ODBRegView::modelUpdated()
{
	for(auto* const field : valueFields())
		field->update();
	for(auto* const group : groups)
		group->adjustWidth();
}

QList<FieldWidget*> ODBRegView::fields() const
{
	QList<FieldWidget*> allFields;
	for(auto* const group : groups)
		allFields.append(group->fields());
	return allFields;
}

QList<ValueField*> ODBRegView::valueFields() const
{
	QList<ValueField*> allValues;
	for(auto* const group : groups)
		allValues.append(group->valueFields());
	return allValues;

}

void ODBRegView::finalize()
{
    for(auto* const field : valueFields())
    {
        ValueField *up=0, *down=0, *left=0, *right=0;
        // now look for unaligned neighbors if no aligned were found
        for(auto* const neighbor : valueFields())
        {
            if(neighbor==field) continue;

            if(field->y()==neighbor->y())
            {
                if(neighbor->x()<field->x() && (!left || left->x()<neighbor->y()))
                    left=neighbor;
                if(neighbor->x()>field->x() && (!right || right->x()>neighbor->x()))
                    right=neighbor;
            }
            else if(neighbor->y() < field->y() && (!up || distSqr(neighbor,field) < distSqr(up,field)))
                up=neighbor;
            else if(neighbor->y() > field->y() && (!down || distSqr(neighbor,field) < distSqr(down,field)))
                down=neighbor;
        }
        field->setNeighbors(up,down,left,right);
    }
}

void ODBRegView::updateFieldsPalette()
{
    for(auto* const field : valueFields())
		field->updatePalette();
}

ValueField* ODBRegView::selectedField() const
{
    for(auto* const field : valueFields())
        if(field->isSelected()) return field;
    return nullptr;
}

void ODBRegView::focusOutEvent(QFocusEvent*)
{
	for(auto* const group : groups)
	{
		auto palette=group->palette();
		palette.setCurrentColorGroup(QPalette::Inactive);
		group->setPalette(palette);
	}
	updateFieldsPalette();
}

void ODBRegView::focusInEvent(QFocusEvent*)
{
	for(auto* const group : groups)
	{
		auto palette=group->palette();
		palette.setCurrentColorGroup(QPalette::Active);
		group->setPalette(palette);
	}
    updateFieldsPalette();
}

void ODBRegView::keyPressEvent(QKeyEvent* event)
{
    auto* const selected=selectedField();
    if(!selected)
    {
        QScrollArea::keyPressEvent(event);
        return;
    }
    switch(event->key())
    {
    case Qt::Key_Up:
        if(selected->up())
            selected->up()->select();
        break;
    case Qt::Key_Down:
        if(selected->down())
            selected->down()->select();
        break;
    case Qt::Key_Left:
        if(selected->left())
            selected->left()->select();
        break;
    case Qt::Key_Right:
        if(selected->right())
            selected->right()->select();
        break;
    default:
        QScrollArea::keyPressEvent(event);
    }
}

}
