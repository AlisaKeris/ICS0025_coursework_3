#include "mainwindow.h"
#include "ui_mainwindow.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    socket = new QLocalSocket(this);

    connect(socket, &QLocalSocket::connected, this, &MainWindow::connected);
    connect(socket, QOverload<QLocalSocket::LocalSocketError>::of(&QLocalSocket::error), this, &MainWindow::errorOccurred);

    // To avoid incorrect input by user
    ui->nPointsText->setValidator( new QIntValidator(0, INT32_MAX, this) );
    ui->orderText->setValidator( new QIntValidator(0, 10, this) );
    ui->x0Text->setValidator( new QDoubleValidator(this) );
    ui->xnText->setValidator( new QDoubleValidator(this) );
}


MainWindow::~MainWindow()
{
   socket->close();
   delete ui;
}

void MainWindow::connected()
{
    if(socket->isValid()){
        // Disable self, enable compute & break buttons
        ui->connectBtn->setEnabled(false);
        ui->computeBtn->setEnabled(true);
        ui->breakBtn->setEnabled(true);
        ui->serverMessageTextArea->setText("Connected");
    }
}

void MainWindow::errorOccurred(QLocalSocket::LocalSocketError error)
{
    qDebug() << error;
    ui->serverMessageTextArea->setText(socket->errorString());
    QMessageBox warning(QMessageBox::Warning, APP_NAME, "Problem connecting.\n\nPlease make sure the server is running and listening for connections.",
    QMessageBox::Ok);
    warning.exec();
}


void MainWindow::on_connectBtn_clicked()
{
    socket->connectToServer(R"(\\.\pipe\ICS0025)");
}

void MainWindow::on_exitBtn_clicked()
{
    socket->abort();
    QApplication::exit();
}

void MainWindow::on_computeBtn_clicked()
{
    ui->serverMessageTextArea->setText("Computing...");

    // Collect parameters
    const std::wstring fnstr = ui->functionComboBox->currentText().toStdWString();
    double xo = ui->x0Text->text().toDouble();
    double xn = ui->xnText->text().toDouble();
    quint32 npoints = ui->nPointsText->text().toInt();
    quint32 order = ui->orderText->text().toInt();

    quint32 packetLength = sizeof(quint32) + (fnstr.size()*sizeof(wchar_t)) + 2*sizeof(uint8_t) + 2*sizeof(double) + 2*sizeof(quint32);

    // Build packet and send
    QByteArray ba;
    QByteArray fnba = QByteArray::fromRawData(reinterpret_cast<const char*>(fnstr.c_str()), fnstr.length()*2);

    ba.append(reinterpret_cast<const char *>(&packetLength), sizeof(quint32));
    ba.append(fnba);
    ba.append((const char)0x0);
    ba.append((const char)0x0);
    ba.append(reinterpret_cast<const char *>(&xo), sizeof(double));
    ba.append(reinterpret_cast<const char *>(&xn), sizeof(double));
    ba.append(reinterpret_cast<const char *>(&npoints), sizeof(quint32));
    ba.append(reinterpret_cast<const char *>(&order), sizeof(quint32));

    socket->write(ba);
    socket->flush();

    // Interpret incoming packet and plot points
    socket->waitForReadyRead();
    QByteArray result = socket->readAll();

    // Verify if result contains "Curve"
    QString controlMsg = QTextCodec::codecForMib(1015)->toUnicode( result.mid(4, 10) );

    if(controlMsg.compare("Curve")!=0){
        // Decode and show error in UI
        result.remove(0, 4);
        ui->serverMessageTextArea->setText(QTextCodec::codecForMib(1015)->toUnicode( result ));
        return;
    }

    ui->serverMessageTextArea->setText("Done!");

    // Clean up data and prepare for plotting
    result.remove(0, 16);
    const double* data = reinterpret_cast<const double*>(result.data());

    QVector<double> x(npoints), y(npoints);
    int xidx=0, yidx=0;
    for (quint32 i=0; i<npoints*2; i++)
    {
      if(i%2==0){
          x[xidx++] = data[i];
      } else{
          y[yidx++] = data[i];
      }
    }
    ui->functionPlot->addGraph();
    ui->functionPlot->graph(0)->setData(x, y);
    ui->functionPlot->xAxis->setLabel("x");
    ui->functionPlot->yAxis->setLabel("y");
    ui->functionPlot->xAxis->setRange(xo, xn);
    ui->functionPlot->yAxis->setRange(*std::min_element(y.constBegin(), y.constEnd()), *std::max_element(y.constBegin(), y.constEnd()));
    ui->functionPlot->replot();
}

void MainWindow::on_breakBtn_clicked()
{
    ui->serverMessageTextArea->setText("Disconnected");

    // Send stop message
    const std::wstring message_wstr = QString("Stop").toStdWString();
    quint32 packetLength = sizeof(quint32) + (message_wstr.size()*sizeof(wchar_t)) + 2*sizeof(uint8_t);

    QByteArray ba;
    QByteArray message_ba = QByteArray::fromRawData(reinterpret_cast<const char*>(message_wstr.c_str()), message_wstr.length()*2);

    ba.append(reinterpret_cast<const char *>(&packetLength), sizeof(quint32));
    ba.append(message_ba);
    ba.append((const char)0x0);
    ba.append((const char)0x0);

    socket->write(ba);
    socket->flush();
    socket->abort();

    // Disable self & compute button, enable connect button
    ui->breakBtn->setEnabled(false);
    ui->computeBtn->setEnabled(false);
    ui->connectBtn->setEnabled(true);
}

void MainWindow::on_functionComboBox_currentIndexChanged(int index)
{
    // Enable order field if Bessel function is chosen
    ui->orderText->setEnabled(index == 2);
}
