pipeline {
  agent {
    docker {
      image 'takigama/platformio:latest'
      args '-u 1000:1000'
    }
  }

  environment {
    BIN_FILE = 'firmware.bin'
    BIN_PATH = ".pio/build/esp32dev/${BIN_FILE}"
    REPO_CREDS = credentials('repository-creds')
    PLATFORMIO_CORE_DIR = "${WORKSPACE}/.platformio"
    HOME = "${WORKSPACE}" // ensures pio uses this path
  }

  stages {
    stage('Initialize Vars') {
      steps {
        script {
          env.PROJECT = "WaterMe"
          env.BRANCH = env.BRANCH_NAME ?: 'unknown'
          env.DATE = sh(script: "date +%Y%m%d", returnStdout: true).trim()
          env.BUILD_NO = env.BUILD_NUMBER
          env.UPLOAD_DIR = "http://repository.fundebazi.com/repository/${env.PROJECT}/${env.BRANCH}/${env.DATE}_${env.BUILD_NO}/"
          env.UPLOAD_URL = "${env.UPLOAD_DIR}${env.BIN_FILE}"
        }
      }
    }

    stage('Checkout') {
      steps {
        checkout scm
      }
    }

    stage('Build Firmware') {
      steps {
        sh 'pio run'
      }
    }

    stage('Prepare Upload Directory') {
      steps {
        script {
          def levels = env.UPLOAD_DIR.replace('http://repository.fundebazi.com/', '').split('/')
          def path = ''
          for (int i = 0; i < levels.size(); i++) {
            path += '/' + levels[i]
            sh """
              curl -sf -u ${env.REPO_CREDS_USR}:${env.REPO_CREDS_PSW} -X MKCOL http://repository.fundebazi.com${path} || true
            """
          }
        }
      }
    }

    stage('Upload Firmware') {
      steps {
        sh """
          curl -u ${env.REPO_CREDS_USR}:${env.REPO_CREDS_PSW} \\
            -X PUT --upload-file ${env.BIN_PATH} \\
            ${env.UPLOAD_URL}
        """
      }
    }

    stage('Verify Upload') {
      steps {
        sh "curl -sI ${env.UPLOAD_URL}"
      }
    }
  }
}
