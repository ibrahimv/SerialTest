﻿#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "controlitem.h"

#include <QDateTime>
#ifdef Q_OS_ANDROID
#include <QBluetoothLocalDevice>
#include <QAndroidJniEnvironment>
#else
#include <QFileDialog>
#endif

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
#ifdef Q_OS_ANDROID
    BTSocket = new QBluetoothSocket(QBluetoothServiceInfo::RfcommProtocol);
    IODevice = BTSocket;
    connect(BTSocket, &QBluetoothSocket::connected, this, &MainWindow::onBTConnectionChanged);
    connect(BTSocket, &QBluetoothSocket::disconnected, this, &MainWindow::onBTConnectionChanged);
    connect(BTSocket, QOverload<QBluetoothSocket::SocketError>::of(&QBluetoothSocket::error), this, &MainWindow::onBTConnectionChanged);
    BTdiscoveryAgent = new QBluetoothDeviceDiscoveryAgent(this);
    connect(BTdiscoveryAgent, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered, this, &MainWindow::BTdeviceDiscovered);
    connect(BTdiscoveryAgent, &QBluetoothDeviceDiscoveryAgent::finished, this, &MainWindow::BTdiscoverFinished);
    connect(BTdiscoveryAgent, QOverload<QBluetoothDeviceDiscoveryAgent::Error>::of(&QBluetoothDeviceDiscoveryAgent::error), this, &MainWindow::BTdiscoverFinished);

    // wider axis for pinch gesture
    ui->qcpWidget->xAxis->setPadding(20);
    ui->qcpWidget->yAxis->setPadding(20);
#else
    serialPort = new QSerialPort();
    IODevice = serialPort;
    connect(serialPort, &QSerialPort::errorOccurred, this, &MainWindow::onSerialErrorOccurred);
    serialPortInfo = new QSerialPortInfo();

    baudRateLabel = new QLabel();
    dataBitsLabel = new QLabel();
    stopBitsLabel = new QLabel();
    parityLabel = new QLabel();
    onTopBox = new QCheckBox(tr("On Top"));
    connect(onTopBox, &QCheckBox::clicked, this, &MainWindow::onTopBoxClicked);

    settings = new QSettings("preference.ini", QSettings::IniFormat);
#endif
    portLabel = new QLabel();
    stateButton = new QPushButton();
    TxLabel = new QLabel();
    RxLabel = new QLabel();
    IODeviceState = false;

    rawReceivedData = new QByteArray();
    rawSendedData = new QByteArray();
    RxUIBuf = new QByteArray();
    plotBuf = new QString();
    plotTracer = new QCPItemTracer(ui->qcpWidget);
    plotText = new QCPItemText(ui->qcpWidget);
    plotDefaultTicker = ui->qcpWidget->xAxis->ticker();
    plotTime = QTime::currentTime();

    repeatTimer = new QTimer();
    updateUITimer = new QTimer();
    updateUITimer->setInterval(1);

    connect(ui->refreshPortsButton, &QPushButton::clicked, this, &MainWindow::refreshPortsInfo);
    connect(ui->sendEdit, &QLineEdit::returnPressed, this, &MainWindow::on_sendButton_clicked);

    connect(IODevice, &QIODevice::readyRead, this, &MainWindow::readData, Qt::QueuedConnection);

    connect(repeatTimer, &QTimer::timeout, this, &MainWindow::on_sendButton_clicked);
    connect(updateUITimer, &QTimer::timeout, this, &MainWindow::updateRxUI);
    connect(stateButton, &QPushButton::clicked, this, &MainWindow::onStateButtonClicked);

    RxSlider = ui->receivedEdit->verticalScrollBar();
    connect(RxSlider, &QScrollBar::valueChanged, this, &MainWindow::onRxSliderValueChanged);
    connect(RxSlider, &QScrollBar::sliderMoved, this, &MainWindow::onRxSliderMoved);

    refreshPortsInfo();
    initUI();

    ui->qcpWidget->axisRect()->setupFullAxesBox(true);
    ui->qcpWidget->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom | QCP::iSelectLegend | QCP::iSelectPlottables);
    ui->qcpWidget->legend->setSelectableParts(QCPLegend::spItems);
    on_plot_dataNumBox_valueChanged(ui->plot_dataNumBox->value());
    on_plot_frameSpTypeBox_currentIndexChanged(ui->plot_frameSpTypeBox->currentIndex());
    on_plot_dataSpTypeBox_currentIndexChanged(ui->plot_dataSpTypeBox->currentIndex());
    plotCounter = 0;
    connect(ui->qcpWidget, &QCustomPlot::legendDoubleClick, this, &MainWindow::onQCPLegendDoubleClick);
    connect(ui->qcpWidget, &QCustomPlot::axisDoubleClick, this, &MainWindow::onQCPAxisDoubleClick);
    plotTracer->setStyle(QCPItemTracer::tsCrosshair);
    plotTracer->setBrush(Qt::red);
    plotTracer->setInterpolating(false);
    plotTracer->setVisible(false);
    plotTracer->setGraph(ui->qcpWidget->graph(plotSelectedId));
    plotText->setPositionAlignment(Qt::AlignTop | Qt::AlignLeft);
    plotText->setTextAlignment(Qt::AlignLeft);
    plotText->position->setType(QCPItemPosition::ptAxisRectRatio);
    plotText->position->setCoords(0.01, 0.01);
    plotText->setPen(QPen(Qt::black));
    plotText->setPadding(QMargins(2, 2, 2, 2));
    plotText->setVisible(false);
    plotSelectedName = ui->qcpWidget->legend->itemWithPlottable(ui->qcpWidget->graph(plotSelectedId))->plottable()->name();
    ui->qcpWidget->replot();
    connect(ui->qcpWidget, &QCustomPlot::selectionChangedByUser, this, &MainWindow::onQCPSelectionChanged);
    connect(ui->qcpWidget->xAxis, QOverload<const QCPRange&>::of(&QCPAxis::rangeChanged), this, &MainWindow::onXAxisChangedByUser);
    plotXAxisWidth = ui->qcpWidget->xAxis->range().size();
    plotTimeTicker->setTimeFormat("%h:%m:%s.%z");
    plotTimeTicker->setTickCount(5);
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::onStateButtonClicked()
{
    QString portName;
#ifdef Q_OS_ANDROID
    portName = BTlastAddress;
#else
    portName = serialPort->portName();
#endif
    if(portName.isEmpty())
    {
        QMessageBox::warning(this, "Error", "Plz connect to a port first");
        return;
    }
    if(IODeviceState)
    {
        IODevice->close();
        onIODeviceDisconnected();
    }
    else
    {
#ifdef Q_OS_ANDROID
        BTSocket->connectToService(QBluetoothAddress(BTlastAddress), QBluetoothUuid::SerialPort);
#else
        IODeviceState = IODevice->open(QIODevice::ReadWrite);
        if(IODeviceState)
            onIODeviceConnected();
        else
            QMessageBox::warning(this, "Error", tr("Cannot open the serial port."));
#endif
    }
}

void MainWindow::initUI()
{
    statusBar()->addWidget(portLabel, 1);
    statusBar()->addWidget(stateButton, 1);
    statusBar()->addWidget(RxLabel, 1);
    statusBar()->addWidget(TxLabel, 1);
#ifdef Q_OS_ANDROID
    ui->baudRateLabel->setVisible(false);
    ui->baudRateBox->setVisible(false);
    ui->advancedBox->setVisible(false);
    ui->portTable->hideColumn(HManufacturer);
    ui->portTable->hideColumn(HSerialNumber);
    ui->portTable->hideColumn(HIsNull);
    ui->portTable->hideColumn(HVendorID);
    ui->portTable->hideColumn(HProductID);
    ui->portTable->hideColumn(HBaudRates);

    ui->portTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->portTable->horizontalHeaderItem(HPortName)->setText(tr("DeviceName"));
    ui->portTable->horizontalHeaderItem(HDescription)->setText(tr("Type"));
    ui->portTable->horizontalHeaderItem(HSystemLocation)->setText(tr("MAC Address"));

    ui->portLabel->setText(tr("MAC Address") + ":");

    // keep screen on

    QAndroidJniObject helper("priv/wh201906/serialtest/BTHelper");
    QtAndroid::runOnAndroidThread([&]
    {
        helper.callMethod<void>("keepScreenOn", "(Landroid/app/Activity;)V", QtAndroid::androidActivity().object());
    });

    // Strange resize behavior on Android
    // Need a fixed size
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setFixedSize(QApplication::primaryScreen()->availableGeometry().size());

#else
    ui->flowControlBox->addItem("NoFlowControl");
    ui->flowControlBox->addItem("HardwareControl");
    ui->flowControlBox->addItem("SoftwareControl");
    ui->flowControlBox->setItemData(0, QSerialPort::NoFlowControl);
    ui->flowControlBox->setItemData(1, QSerialPort::HardwareControl);
    ui->flowControlBox->setItemData(2, QSerialPort::SoftwareControl);
    ui->parityBox->addItem("NoParity");
    ui->parityBox->addItem("EvenParity");
    ui->parityBox->addItem("OddParity");
    ui->parityBox->addItem("SpaceParity");
    ui->parityBox->addItem("MarkParity");
    ui->parityBox->setItemData(0, QSerialPort::NoParity);
    ui->parityBox->setItemData(1, QSerialPort::EvenParity);
    ui->parityBox->setItemData(2, QSerialPort::OddParity);
    ui->parityBox->setItemData(3, QSerialPort::SpaceParity);
    ui->parityBox->setItemData(4, QSerialPort::MarkParity);
    ui->stopBitsBox->addItem("1");
    ui->stopBitsBox->addItem("1.5");
    ui->stopBitsBox->addItem("2");
    ui->stopBitsBox->setItemData(0, QSerialPort::OneStop);
    ui->stopBitsBox->setItemData(1, QSerialPort::OneAndHalfStop);
    ui->stopBitsBox->setItemData(2, QSerialPort::TwoStop);
    ui->dataBitsBox->addItem("5");
    ui->dataBitsBox->addItem("6");
    ui->dataBitsBox->addItem("7");
    ui->dataBitsBox->addItem("8");
    ui->dataBitsBox->setItemData(0, QSerialPort::Data5);
    ui->dataBitsBox->setItemData(1, QSerialPort::Data6);
    ui->dataBitsBox->setItemData(2, QSerialPort::Data7);
    ui->dataBitsBox->setItemData(3, QSerialPort::Data8);
    ui->dataBitsBox->setCurrentIndex(3);

    statusBar()->addWidget(baudRateLabel, 1);
    statusBar()->addWidget(dataBitsLabel, 1);
    statusBar()->addWidget(stopBitsLabel, 1);
    statusBar()->addWidget(parityLabel, 1);
    statusBar()->addWidget(onTopBox, 1);
    dockInit();
#endif

    stateButton->setMinimumHeight(1);
    stateButton->setStyleSheet("*{text-align:left;}");


    on_advancedBox_clicked(false);
    on_plot_advancedBox_stateChanged(Qt::Unchecked);
    stateUpdate();
}

void MainWindow::refreshPortsInfo()
{
    ui->portTable->clearContents();
    ui->portBox->clear();
#ifdef Q_OS_ANDROID
    ui->refreshPortsButton->setText(tr("Searching..."));
    QAndroidJniEnvironment env;
    QtAndroid::PermissionResult r = QtAndroid::checkPermission("android.permission.ACCESS_FINE_LOCATION");
    if(r == QtAndroid::PermissionResult::Denied)
    {
        QtAndroid::requestPermissionsSync(QStringList() << "android.permission.ACCESS_FINE_LOCATION");
        r = QtAndroid::checkPermission("android.permission.ACCESS_FINE_LOCATION");
        if(r == QtAndroid::PermissionResult::Denied)
        {
            qDebug() << "failed to request";
        }
    }
    qDebug() << "has permission";

    QAndroidJniObject helper("priv/wh201906/serialtest/BTHelper");
    qDebug() << "test:" << helper.callObjectMethod<jstring>("TestStr").toString();
    QAndroidJniObject array = helper.callObjectMethod("getBondedDevices", "()[Ljava/lang/String;");
    int arraylen = env->GetArrayLength(array.object<jarray>());
    qDebug() << "arraylen:" << arraylen;
    ui->portTable->setRowCount(arraylen);
    for(int i = 0; i < arraylen; i++)
    {
        QString info = QAndroidJniObject::fromLocalRef(env->GetObjectArrayElement(array.object<jobjectArray>(), i)).toString();
        QString address = info.left(info.indexOf(' '));
        QString name = info.right(info.length() - info.indexOf(' ') - 1);
        qDebug() << address << name;
        ui->portTable->setItem(i, HPortName, new QTableWidgetItem(name));
        ui->portTable->setItem(i, HSystemLocation, new QTableWidgetItem(address));
        ui->portTable->setItem(i, HDescription, new QTableWidgetItem("Bonded"));
        ui->portBox->addItem(address);
    }

    BTdiscoveryAgent->start();
#else
    QList<QSerialPortInfo> ports = QSerialPortInfo::availablePorts();
    ui->portTable->setRowCount(ports.size());
    for(int i = 0; i < ports.size(); i++)
    {
        ui->portTable->setItem(i, HPortName, new QTableWidgetItem(ports[i].portName()));
        ui->portBox->addItem(ports[i].portName());
        ui->portTable->setItem(i, HDescription, new QTableWidgetItem(ports[i].description()));
        ui->portTable->setItem(i, HManufacturer, new QTableWidgetItem(ports[i].manufacturer()));
        ui->portTable->setItem(i, HSerialNumber, new QTableWidgetItem(ports[i].serialNumber()));
        ui->portTable->setItem(i, HIsNull, new QTableWidgetItem(ports[i].isNull() ? "Yes" : "No"));
        ui->portTable->setItem(i, HSystemLocation, new QTableWidgetItem(ports[i].systemLocation()));
        ui->portTable->setItem(i, HVendorID, new QTableWidgetItem(QString::number(ports[i].vendorIdentifier())));
        ui->portTable->setItem(i, HProductID, new QTableWidgetItem(QString::number(ports[i].productIdentifier())));

        QList<qint32> baudRateList = ports[i].standardBaudRates();
        QString baudRates = "";
        for(int j = 0; j < baudRates.size(); j++)
        {
            baudRates += QString::number(baudRateList[j]) + ", ";
        }
        ui->portTable->setItem(i, HBaudRates, new QTableWidgetItem(baudRates));
    }
#endif
}

void MainWindow::on_portTable_cellDoubleClicked(int row, int column)
{
    Q_UNUSED(column);
    ui->portBox->setCurrentIndex(row);
#ifndef Q_OS_ANDROID
    QStringList preferences = settings->childGroups();
    QStringList::iterator it;


    // search preference by <vendorID>-<productID>
    QString id = ui->portTable->item(row, HVendorID)->text();  // vendor id
    id += "-";
    id += ui->portTable->item(row, HProductID)->text(); // product id
    for(it = preferences.begin(); it != preferences.end(); it++)
    {
        if(*it == id)
        {
            loadPreference(id);
            break;
        }
    }
    if(it != preferences.end())
        return;

    // search preference by PortName
    id = ui->portTable->item(row, HPortName)->text();
    for(it = preferences.begin(); it != preferences.end(); it++)
    {
        if(*it == id)
        {
            loadPreference(id);
            break;
        }
    }
#endif
}

void MainWindow::on_advancedBox_clicked(bool checked)
{
    ui->dataBitsLabel->setVisible(checked);
    ui->dataBitsBox->setVisible(checked);
    ui->stopBitsLabel->setVisible(checked);
    ui->stopBitsBox->setVisible(checked);
    ui->parityLabel->setVisible(checked);
    ui->parityBox->setVisible(checked);
    ui->flowControlLabel->setVisible(checked);
    ui->flowControlBox->setVisible(checked);
}

void MainWindow::on_openButton_clicked()
{
#ifdef Q_OS_ANDROID
    BTSocket->connectToService(QBluetoothAddress(ui->portBox->currentText()), QBluetoothUuid::SerialPort);
#else
    serialPort->setPortName(ui->portBox->currentText());
    serialPort->setBaudRate(ui->baudRateBox->currentText().toInt());
    serialPort->setDataBits((QSerialPort::DataBits)ui->dataBitsBox->currentData().toInt());
    serialPort->setStopBits((QSerialPort::StopBits)ui->stopBitsBox->currentData().toInt());
    serialPort->setParity((QSerialPort::Parity)ui->parityBox->currentData().toInt());
    serialPort->setFlowControl((QSerialPort::FlowControl)ui->flowControlBox->currentData().toInt());
    if(serialPort->isOpen())
    {
        QMessageBox::warning(this, "Error", "The port has been opened.");
        return;
    }
    if(!serialPort->open(QSerialPort::ReadWrite))
    {
        QMessageBox::warning(this, "Error", tr("Cannot open the serial port."));
        return;
    }
    onIODeviceConnected();
    savePreference(serialPort->portName());
#endif
}

void MainWindow::on_closeButton_clicked()
{
    IODevice->close();
    onIODeviceDisconnected();
}

void MainWindow::stateUpdate()
{

    QString portName;
#ifdef Q_OS_ANDROID
    portName = BTSocket->peerName();
#else
    portName = serialPort->portName();
    QString stopbits[4] = {"", "OneStop", "TwoStop", "OneAndHalfStop"};
    QString parities[6] = {"NoParity", "", "EvenParity", "OddParity", "SpaceParity", "MarkParity"};
    if(IODeviceState)
    {
        baudRateLabel->setText("BaudRate: " + QString::number(serialPort->baudRate()));
        dataBitsLabel->setText("DataBits: " + QString::number(serialPort->dataBits()));
        stopBitsLabel->setText("StopBits: " + stopbits[(int)serialPort->stopBits()]);
        parityLabel->setText("Parity: " + parities[(int)serialPort->parity()]);
    }
    else
    {
        baudRateLabel->setText("BaudRate: ");
        dataBitsLabel->setText("DataBits: ");
        stopBitsLabel->setText("StopBits: ");
        parityLabel->setText("Parity: ");
    }
#endif
    if(IODeviceState)
        stateButton->setText("State: √");
    else
        stateButton->setText("State: X");
    portLabel->setText("Port: " + portName);
    RxLabel->setText("Rx: " + QString::number(rawReceivedData->length()));
    TxLabel->setText("Tx: " + QString::number(rawSendedData->length()));
}

void MainWindow::onIODeviceConnected()
{
    qDebug() << "IODevice Connected";
    IODeviceState = true;
    updateUITimer->start();
    stateUpdate();
    refreshPortsInfo();
}

void MainWindow::onIODeviceDisconnected()
{
    qDebug() << "IODevice Disconnected";
    IODeviceState = false;
    updateUITimer->stop();
    stateUpdate();
    refreshPortsInfo();
    updateRxUI();
}

// Rx/Tx Data
// **********************************************************************************************************************************************

void MainWindow::syncReceivedEditWithData()
{
    RxSlider->blockSignals(true);
    if(isReceivedDataHex)
        ui->receivedEdit->setPlainText(rawReceivedData->toHex(' ') + ' ');
    else
        ui->receivedEdit->setPlainText(*rawReceivedData);
    RxSlider->blockSignals(false);
//    qDebug() << toHEX(*rawReceivedData);
}

void MainWindow::syncSendedEditWithData()
{
    if(isSendedDataHex)
        ui->sendedEdit->setPlainText(rawSendedData->toHex(' ') + ' ');
    else
        ui->sendedEdit->setPlainText(*rawSendedData);
}

// TODO:
// split sync process, add processEvents()
// void MainWindow::syncEditWithData()

void MainWindow::appendReceivedData(QByteArray& data)
{
    int cursorPos;
    int sliderPos;
    bool chopped = false;
    sliderPos = RxSlider->sliderPosition();

    // if \r and \n are received seperatedly, the rawReceivedData will be fine, but the receivedEdit will have a empty line
    // just ignore one of them
    if(!data.isEmpty() && *(data.end() - 1) == '\r')
    {
        data.chop(1);
        chopped = true;
    }

    cursorPos = ui->receivedEdit->textCursor().position();
    ui->receivedEdit->moveCursor(QTextCursor::End);
    if(isReceivedDataHex)
    {
        ui->receivedEdit->insertPlainText(data.toHex(' ') + ' ');
        hexCounter += data.length();
        // QPlainTextEdit is not good at handling long line
        // Seperate for better realtime receiving response
        if(hexCounter > 5000)
        {
            ui->receivedEdit->insertPlainText("\r\n");
            hexCounter = 0;
        }
    }
    else
        ui->receivedEdit->insertPlainText(data);
    if(chopped)
        data.append('\r'); // undo data.chop(1);
    ui->receivedEdit->textCursor().setPosition(cursorPos);
    RxSlider->setSliderPosition(sliderPos);
}

void MainWindow::readData()
{
    QByteArray newData = IODevice->readAll();
    if(newData.isEmpty())
        return;
    rawReceivedData->append(newData);
    if(ui->receivedLatestBox->isChecked())
    {
        userRequiredRxSliderPos = RxSlider->maximum();
        RxSlider->setSliderPosition(RxSlider->maximum());
    }
    else
    {
        userRequiredRxSliderPos = currRxSliderPos;
        RxSlider->setSliderPosition(currRxSliderPos);
    }
    RxLabel->setText("Rx: " + QString::number(rawReceivedData->length()));
    RxUIBuf->append(newData);
    QApplication::processEvents();
}

void MainWindow::on_sendButton_clicked()
{
    QByteArray data;
    if(isSendedDataHex)
        data = QByteArray::fromHex(ui->sendEdit->text().toLatin1());
    else
        data = ui->sendEdit->text().toLatin1();
    if(ui->data_suffixBox->isChecked())
    {
        if(ui->data_suffixTypeBox->currentIndex() == 0)
            data += ui->data_suffixEdit->text().toLatin1();
        else if(ui->data_suffixTypeBox->currentIndex() == 1)
            data += QByteArray::fromHex(ui->data_suffixEdit->text().toLatin1());
        else if(ui->data_suffixTypeBox->currentIndex() == 2)
            data += "\r\n";
    }

    sendData(data);
}

void MainWindow::sendData(QByteArray& data)
{
    if(!IODeviceState)
    {
        QMessageBox::warning(this, "Error", "No port is opened.");
        return;
    }
    rawSendedData->append(data);
    syncSendedEditWithData();
    IODevice->write(data);
    TxLabel->setText("Tx: " + QString::number(rawSendedData->length()));
}

// Rx/Tx UI
// **********************************************************************************************************************************************

void MainWindow::onRxSliderValueChanged(int value)
{
    // qDebug() << "valueChanged" << value;
    currRxSliderPos = value;
}

void MainWindow::onRxSliderMoved(int value)
{
    // slider is moved by user
    // qDebug() << "sliderMoved" << value;
    userRequiredRxSliderPos = value;
}

void MainWindow::on_sendedHexBox_stateChanged(int arg1)
{
    isSendedDataHex = (arg1 == Qt::Checked);
    syncSendedEditWithData();
}

void MainWindow::on_receivedHexBox_stateChanged(int arg1)
{
    isReceivedDataHex = (arg1 == Qt::Checked);
    syncReceivedEditWithData();
}

void MainWindow::on_receivedClearButton_clicked()
{
    rawReceivedData->clear();
    RxLabel->setText("Rx: " + QString::number(rawReceivedData->length()));
    syncReceivedEditWithData();
}

void MainWindow::on_sendedClearButton_clicked()
{
    rawSendedData->clear();
    TxLabel->setText("Tx: " + QString::number(rawSendedData->length()));
    syncSendedEditWithData();
}

void MainWindow::on_sendEdit_textChanged(const QString &arg1)
{
    Q_UNUSED(arg1);
    repeatTimer->stop();
    ui->data_repeatBox->setChecked(false);
}

void MainWindow::on_data_repeatBox_clicked(bool checked)
{
    if(checked)
    {
        repeatTimer->setInterval(ui->repeatDelayEdit->text().toInt());
        repeatTimer->start();
    }
    else
        repeatTimer->stop();
}

void MainWindow::on_receivedCopyButton_clicked()
{
    QApplication::clipboard()->setText(ui->receivedEdit->toPlainText());
}

void MainWindow::on_sendedCopyButton_clicked()
{
    QApplication::clipboard()->setText(ui->sendedEdit->toPlainText());
}

void MainWindow::on_receivedExportButton_clicked()
{
    bool flag = true;
    QString fileName, selection;
    fileName = QFileDialog::getSaveFileName(this, tr("Export received data"), QDateTime::currentDateTime().toString("yyyy-MM-dd_hh-mm-ss") + ".txt");
    if(fileName.isEmpty())
        return;
    QFile file(fileName);
    flag &= file.open(QFile::WriteOnly | QFile::Text);
    selection = ui->receivedEdit->textCursor().selectedText().replace(QChar(0x2029), '\n');
    if(selection.isEmpty())
        flag &= file.write(ui->receivedEdit->toPlainText().toUtf8()) != -1;
    else
        flag &= file.write(selection.replace(QChar(0x2029), '\n').toUtf8()) != -1;
    file.close();
    QMessageBox::information(this, tr("Info"), flag ? tr("Successed!") : tr("Failed!"));
}

void MainWindow::on_sendedExportButton_clicked()
{
    bool flag = true;
    QString fileName, selection;
    fileName = QFileDialog::getSaveFileName(this, tr("Export sended data"), QDateTime::currentDateTime().toString("yyyy-MM-dd_hh-mm-ss") + ".txt");
    if(fileName.isEmpty())
        return;
    QFile file(fileName);
    flag &= file.open(QFile::WriteOnly | QFile::Text);
    selection = ui->sendedEdit->textCursor().selectedText().replace(QChar(0x2029), '\n');
    if(selection.isEmpty())
        flag &= file.write(ui->sendedEdit->toPlainText().toUtf8()) != -1;
    else
        flag &= file.write(selection.replace(QChar(0x2029), '\n').toUtf8()) != -1;
    file.close();
    QMessageBox::information(this, tr("Info"), flag ? tr("Successed!") : tr("Failed!"));
}

void MainWindow::on_receivedUpdateButton_clicked()
{
    syncReceivedEditWithData();
}

// plot related
// **********************************************************************************************************************************************

void MainWindow::onXAxisChangedByUser(const QCPRange &newRange)
{
    plotXAxisWidth = newRange.size();
}

void MainWindow::updateRxUI()
{
    double currKey;
    bool hasData = false;
    if(RxUIBuf->isEmpty())
        return;
    if(ui->receivedRealtimeBox->isChecked())
        appendReceivedData(*RxUIBuf);
    if(ui->plot_enaBox->isChecked())
    {
        int i;
        QStringList dataList;
        plotBuf->append(*RxUIBuf);
        while((i = plotBuf->indexOf(plotFrameSeparator)) != -1)
        {
            hasData = true;
            dataList = ((QString)(plotBuf->left(i))).split(plotDataSeparator);
            plotBuf->remove(0, i + ui->plot_dataSpEdit->text().length());
            plotCounter++;
            if(ui->plot_XTypeBox->currentIndex() == 0)
            {
                currKey = plotCounter;
                for(i = 0; i < ui->plot_dataNumBox->value() && i < dataList.length(); i++)
                    ui->qcpWidget->graph(i)->addData(currKey, dataList[i].toDouble());
            }
            else if(ui->plot_XTypeBox->currentIndex() == 1)
            {
                currKey = dataList[0].toDouble();
                for(i = 1; i < ui->plot_dataNumBox->value() && i < dataList.length(); i++)
                    ui->qcpWidget->graph(i - 1)->addData(currKey, dataList[i].toDouble());
            }
            else if(ui->plot_XTypeBox->currentIndex() == 2)
            {
                currKey = plotTime.msecsTo(QTime::currentTime()) / 1000.0;
                for(i = 0; i < ui->plot_dataNumBox->value() && i < dataList.length(); i++)
                    ui->qcpWidget->graph(i)->addData(currKey, dataList[i].toDouble());
            }

        }
        if(!hasData)
        {
            RxUIBuf->clear();
            return;
        }
        if(ui->plot_latestBox->isChecked())
        {
            ui->qcpWidget->xAxis->blockSignals(true);
            ui->qcpWidget->xAxis->setRange(currKey, plotXAxisWidth, Qt::AlignRight);
            ui->qcpWidget->xAxis->blockSignals(false);
            if(ui->plot_tracerCheckBox->isChecked())
                updateTracer(currKey);
        }
        ui->qcpWidget->replot(QCustomPlot::rpQueuedReplot);
    }
    RxUIBuf->clear();
}

void MainWindow::on_plot_dataNumBox_valueChanged(int arg1)
{
    if(ui->plot_XTypeBox->currentIndex() == 1) // use first data as X
        arg1--;
    int delta = arg1 - ui->qcpWidget->graphCount();
    QCPGraph* currGraph;
    if(delta > 0)
    {
        for(int i = 0; i < delta; i++)
        {
            QRandomGenerator* randGen = QRandomGenerator::global();
            currGraph = ui->qcpWidget->addGraph();
            currGraph->setPen(QColor(randGen->bounded(10, 235), randGen->bounded(10, 235), randGen->bounded(10, 235)));
            currGraph->setSelectable(QCP::stWhole);
        }
    }
    else if(delta < 0)
    {
        delta = -delta;
        for(int i = 0; i < delta; i++)
            ui->qcpWidget->removeGraph(ui->qcpWidget->graphCount() - 1);
    }
}

void MainWindow::on_plot_clearButton_clicked()
{
    int num;
    plotCounter = 0;
    plotTime = QTime::currentTime();
    num = ui->qcpWidget->graphCount();
    for(int i = 0; i < num; i++)
        ui->qcpWidget->graph(i)->data()->clear(); // use data()->clear() rather than data().clear()
    ui->qcpWidget->replot();
}

void MainWindow::on_plot_legendCheckBox_stateChanged(int arg1)
{
    ui->qcpWidget->legend->setVisible(arg1 == Qt::Checked);
    ui->qcpWidget->replot();
}

void MainWindow::on_plot_advancedBox_stateChanged(int arg1)
{
    ui->plot_advancedWidget->setVisible(arg1 == Qt::Checked);
}

void MainWindow::onQCPLegendDoubleClick(QCPLegend *legend, QCPAbstractLegendItem *item)
{
    // Rename a graph by double clicking on its legend item
    Q_UNUSED(legend)
    if(item)  // only react if item was clicked (user could have clicked on border padding of legend where there is no item, then item is 0)
    {
        QCPPlottableLegendItem *plItem = qobject_cast<QCPPlottableLegendItem*>(item);
        bool ok;
        QString newName = QInputDialog::getText(this, tr("Legend:"), tr("New graph name:"), QLineEdit::Normal, plItem->plottable()->name(), &ok);
        if(ok)
        {
            plItem->plottable()->setName(newName);
            ui->qcpWidget->replot();
        }
    }
}

void MainWindow::onQCPAxisDoubleClick(QCPAxis *axis)
{
    if(axis == ui->qcpWidget->xAxis)
        on_plot_fitXButton_clicked();
    else if(axis == ui->qcpWidget->yAxis)
        on_plot_fitYButton_clicked();
}

void MainWindow::onQCPMouseMoved(QMouseEvent *event)
{
    if(ui->plot_tracerCheckBox->isChecked())
    {
        qDebug() << event->pos();
        double x = ui->qcpWidget->xAxis->pixelToCoord(event->pos().x());
        updateTracer(x);
    }
}

void MainWindow::updateTracer(double x)
{
    plotTracer->setGraphKey(x);
    plotTracer->updatePosition();

    double xValue = plotTracer->position->key();
    double yValue = plotTracer->position->value();
    plotText->setText((plotSelectedName + "\n%2, %3").arg(xValue).arg(yValue));
    ui->qcpWidget->replot();
}

void MainWindow::on_plot_tracerCheckBox_stateChanged(int arg1)
{
    if(arg1 == Qt::Checked)
    {
        connect(ui->qcpWidget, &QCustomPlot::mouseMove, this, &MainWindow::onQCPMouseMoved);
        plotTracer->setVisible(true);
        plotText->setVisible(true);
    }
    else
    {
        disconnect(ui->qcpWidget, &QCustomPlot::mouseMove, this, &MainWindow::onQCPMouseMoved);
        plotTracer->setVisible(false);
        plotText->setVisible(false);
    }
    ui->qcpWidget->replot();
}

void MainWindow::on_plot_fitXButton_clicked()
{
    ui->qcpWidget->xAxis->rescale(true);
    ui->qcpWidget->replot();
}

void MainWindow::on_plot_fitYButton_clicked()
{
    ui->qcpWidget->yAxis->rescale(true);
    ui->qcpWidget->replot();
}

void MainWindow::onQCPSelectionChanged()
{
    // Copied from official interaction demo
    // A legendItem and a plottable cannot be both selected by user.
    // synchronize selection of graphs with selection of corresponding legend items:
    QCPGraph *graph;
    for(int i = 0; i < ui->qcpWidget->graphCount(); ++i)
    {
        graph = ui->qcpWidget->graph(i);
        QCPPlottableLegendItem *item = ui->qcpWidget->legend->itemWithPlottable(graph);
        if(item->selected() || graph->selected())
        {
            plotSelectedId = i;
            item->setSelected(true);
            graph->setSelection(QCPDataSelection(graph->data()->dataRange()));
        }
    }
    graph = ui->qcpWidget->graph(plotSelectedId);
    plotTracer->setGraph(graph);
    plotSelectedName = ui->qcpWidget->legend->itemWithPlottable(graph)->plottable()->name();
}

void MainWindow::on_plot_frameSpTypeBox_currentIndexChanged(int index)
{
    ui->plot_frameSpEdit->setVisible(index != 2);
    if(index == 0)
        plotFrameSeparator = ui->plot_frameSpEdit->text();
    else if(index == 1)
        plotFrameSeparator = QByteArray::fromHex(ui->plot_frameSpEdit->text().toLatin1());
    else if(index == 2)
        plotFrameSeparator = "\r\n";
}

void MainWindow::on_plot_dataSpTypeBox_currentIndexChanged(int index)
{
    ui->plot_dataSpEdit->setVisible(index != 2);
    if(index == 0)
        plotDataSeparator = ui->plot_dataSpEdit->text();
    else if(index == 1)
        plotDataSeparator = QByteArray::fromHex(ui->plot_dataSpEdit->text().toLatin1());
    else if(index == 2)
        plotDataSeparator = "\r\n";
}

void MainWindow::on_plot_XTypeBox_currentIndexChanged(int index)
{
    if(index == 1 && ui->plot_dataNumBox->value() == 1) // use first data as X axis
    {
        ui->plot_dataNumBox->setValue(2);
        ui->plot_dataNumBox->setMinimum(2);
    }
    else
    {
        ui->plot_dataNumBox->setMinimum(1);
    }
    if(index == 2)
    {
        ui->qcpWidget->xAxis->setTicker(plotTimeTicker);
    }
    else
    {
        ui->qcpWidget->xAxis->setTicker(plotDefaultTicker);
    }
}

// platform specific
// **********************************************************************************************************************************************

#ifdef Q_OS_ANDROID
void MainWindow::BTdiscoverFinished()
{
    ui->refreshPortsButton->setText(tr("Refresh"));
}

void MainWindow::BTdeviceDiscovered(const QBluetoothDeviceInfo &device)
{
    QString address = device.address().toString();
    QString name = device.name();
    int i = ui->portTable->rowCount();
    ui->portTable->setRowCount(i + 1);
    ui->portTable->setItem(i, HPortName, new QTableWidgetItem(name));
    ui->portTable->setItem(i, HSystemLocation, new QTableWidgetItem(address));
    ui->portTable->setItem(i, HDescription, new QTableWidgetItem("Discovered"));
    ui->portBox->addItem(address);
    qDebug() << name
             << address
             << device.isValid()
             << device.rssi()
             << device.majorDeviceClass()
             << device.minorDeviceClass()
             << device.serviceClasses()
             << device.manufacturerData();
}

void MainWindow::onBTConnectionChanged()
{
    if(BTSocket->isOpen())
    {
        onIODeviceConnected();
        BTlastAddress = ui->portBox->currentText();
    }
    else
        onIODeviceDisconnected();
}
#else

void MainWindow::dockInit()
{
    setDockNestingEnabled(true);
    QDockWidget* dock;
    QWidget* widget;
    int count = ui->funcTab->count();
    for(int i = 0; i < count; i++)
    {
        dock = new QDockWidget(ui->funcTab->tabText(0), this);
        qDebug() << "dock name" << ui->funcTab->tabText(0);
        dock->setFeatures(QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetMovable);// movable is necessary, otherwise the dock cannot be dragged
        dock->setAllowedAreas(Qt::AllDockWidgetAreas);
        dock->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        widget = ui->funcTab->widget(0);
        dock->setWidget(widget);
        addDockWidget(Qt::BottomDockWidgetArea, dock);
        if(!dockList.isEmpty())
            tabifyDockWidget(dockList[0], dock);
        dockList.append(dock);
    }
    ui->funcTab->setVisible(false);
    ui->centralwidget->setVisible(false);
    dockList[0]->setVisible(true);
    dockList[0]->raise();
}

void MainWindow::onTopBoxClicked(bool checked)
{
    setWindowFlag(Qt::WindowStaysOnTopHint, checked);
    show();
}

void MainWindow::onSerialErrorOccurred(QSerialPort::SerialPortError error)
{
    qDebug() << error;
    if(error != QSerialPort::NoError && IODeviceState)
    {
        IODevice->close();
        onIODeviceDisconnected();
    }

}

void MainWindow::savePreference(const QString& portName)
{
    QSerialPortInfo info(portName);
    QString id;
    if(info.vendorIdentifier() != 0 && info.productIdentifier() != 0)
        id = QString::number(info.vendorIdentifier()) + "-" + QString::number(info.productIdentifier());
    else
        id = portName;
    settings->beginGroup(id);
    settings->setValue("BaudRate", ui->baudRateBox->currentText());
    settings->setValue("DataBitsID", ui->dataBitsBox->currentIndex());
    settings->setValue("StopBitsID", ui->stopBitsBox->currentIndex());
    settings->setValue("ParityID", ui->parityBox->currentIndex());
    settings->setValue("FlowControlID", ui->flowControlBox->currentIndex());
    settings->endGroup();
}

void MainWindow::loadPreference(const QString& id)
{
    settings->beginGroup(id);
    ui->baudRateBox->setEditText(settings->value("BaudRate").toString());
    ui->dataBitsBox->setCurrentIndex(settings->value("DataBitsID").toInt());
    ui->stopBitsBox->setCurrentIndex(settings->value("StopBitsID").toInt());
    ui->parityBox->setCurrentIndex(settings->value("ParityID").toInt());
    ui->flowControlBox->setCurrentIndex(settings->value("FlowControlID").toInt());
    settings->endGroup();
}
#endif


void MainWindow::on_ctrl_addCMDButton_clicked()
{
    QBoxLayout* p = static_cast<QBoxLayout*>(ui->ctrl_itemContents->layout());
    ControlItem* c = new ControlItem(ControlItem::Command);
    connect(c, &ControlItem::send, this, &MainWindow::sendData);
    connect(c, &ControlItem::destroyed, this, &MainWindow::onCtrlItemDestroyed);
    p->insertWidget(ctrlItemCount++, c);
}


void MainWindow::on_ctrl_addSliderButton_clicked()
{
    QBoxLayout* p = static_cast<QBoxLayout*>(ui->ctrl_itemContents->layout());
    ControlItem* c = new ControlItem(ControlItem::Slider);
    connect(c, &ControlItem::send, this, &MainWindow::sendData);
    connect(c, &ControlItem::destroyed, this, &MainWindow::onCtrlItemDestroyed);
    p->insertWidget(ctrlItemCount++, c);
}


void MainWindow::on_ctrl_addCheckBoxButton_clicked()
{
    QBoxLayout* p = static_cast<QBoxLayout*>(ui->ctrl_itemContents->layout());
    ControlItem* c = new ControlItem(ControlItem::CheckBox);
    connect(c, &ControlItem::send, this, &MainWindow::sendData);
    connect(c, &ControlItem::destroyed, this, &MainWindow::onCtrlItemDestroyed);
    p->insertWidget(ctrlItemCount++, c);
}


void MainWindow::on_ctrl_addSpinBoxButton_clicked()
{
    QBoxLayout* p = static_cast<QBoxLayout*>(ui->ctrl_itemContents->layout());
    ControlItem* c = new ControlItem(ControlItem::SpinBox);
    connect(c, &ControlItem::send, this, &MainWindow::sendData);
    connect(c, &ControlItem::destroyed, this, &MainWindow::onCtrlItemDestroyed);
    p->insertWidget(ctrlItemCount++, c);
}

void MainWindow::onCtrlItemDestroyed()
{
    ctrlItemCount--;
}


void MainWindow::on_ctrl_clearButton_clicked()
{
    const QList<ControlItem*> list = ui->ctrl_itemContents->findChildren<ControlItem*>(QString(), Qt::FindDirectChildrenOnly);
    for(auto it = list.begin(); it != list.end(); it++)
        (*it)->deleteLater();
}

void MainWindow::on_data_suffixTypeBox_currentIndexChanged(int index)
{
    ui->data_suffixEdit->setVisible(index != 2);
    ui->data_suffixEdit->setPlaceholderText(tr("Suffix") + ((index == 1) ? "(Hex)" : ""));
}


void MainWindow::on_ctrl_importButton_clicked()
{
    bool flag = true;
    const QList<ControlItem*> list = ui->ctrl_itemContents->findChildren<ControlItem*>(QString(), Qt::FindDirectChildrenOnly);
    QString fileName;
    QBoxLayout* p = static_cast<QBoxLayout*>(ui->ctrl_itemContents->layout());
    fileName = QFileDialog::getOpenFileName(this, tr("Import Control Panel"));
    if(fileName.isEmpty())
        return;
    QFile file(fileName);
    flag &= file.open(QFile::ReadOnly | QFile::Text);
    QStringList dataList = QString(file.readAll()).split("\n", Qt::SkipEmptyParts);
    for(auto it = dataList.begin(); it != dataList.end(); it++)
    {
        if(it->at(0) == '#')
            continue;
        ControlItem* c = new ControlItem(ControlItem::Command);
        connect(c, &ControlItem::send, this, &MainWindow::sendData);
        connect(c, &ControlItem::destroyed, this, &MainWindow::onCtrlItemDestroyed);
        p->insertWidget(ctrlItemCount++, c);
        if(!c->load(*it))
            c->deleteLater();
    }
    file.close();
    QMessageBox::information(this, tr("Info"), flag ? tr("Successed!") : tr("Failed!"));
}


void MainWindow::on_ctrl_exportButton_clicked()
{
    if(ctrlItemCount == 0)
    {
        QMessageBox::information(this, tr("Info"), tr("Please add item first"));
        return;
    }
    bool flag = true;
    const QList<ControlItem*> list = ui->ctrl_itemContents->findChildren<ControlItem*>(QString(), Qt::FindDirectChildrenOnly);
    QString fileName;
    fileName = QFileDialog::getSaveFileName(this, tr("Export Control Panel"), QDateTime::currentDateTime().toString("yyyy-MM-dd_hh-mm-ss") + ".txt");
    if(fileName.isEmpty())
        return;
    QFile file(fileName);
    flag &= file.open(QFile::WriteOnly | QFile::Text);
    for(auto it = list.begin(); it != list.end(); it++)
        flag &= file.write(((*it)->save() + "\n").toUtf8()) != -1;
    file.close();
    QMessageBox::information(this, tr("Info"), flag ? tr("Successed!") : tr("Failed!"));
}

