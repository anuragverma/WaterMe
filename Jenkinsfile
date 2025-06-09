pipeline {
  agent {
    docker {
      image 'platformio/platformio-core:latest'
      args '-u 1000:1000'
    }
  }

  environment {
    ARTIFACT = 'firmware.bin'
    BASE_URL = 'http://repository.fundebazi.com/repository'
    DATE_TAG = sh(script: 'date +%Y%m%d', returnStdout: true).trim()
  }

  stages {
    stage('Init') {
      steps {
        script {
          env.PROJECT_DIR = sh(script: "basename -s .git \$(git config --get remote.origin.url)", returnStdout: true).trim()
          env.VERSION_DIR = "${env.DATE_TAG}_${env.BUILD_NUMBER}"
          env.UPLOAD_DIR = "${env.BASE_URL}/${env.PROJECT_DIR}/${env.VERSION_DIR}"
          env.UPLOAD_URL = "${env.UPLOAD_DIR}/${env.ARTIFACT}"
        }
      }
    }

    stage('Build Firmware') {
      steps {
        dir("${env.PROJECT_DIR}") {
          sh 'platformio run'
        }
        sh "ls -lh ${env.PROJECT_DIR}/.pio/build/esp32dev"
      }
    }

    stage('Upload Firmware') {
      steps {
        script {
          def artifactPath = "${env.PROJECT_DIR}/.pio/build/esp32dev/${env.ARTIFACT}"
          sh """
            curl -u uploader:$REPO_PASS -X MKCOL ${env.BASE_URL} || true
            curl -u uploader:$REPO_PASS -X MKCOL ${env.BASE_URL}/${env.PROJECT_DIR} || true
            curl -u uploader:$REPO_PASS -X MKCOL ${env.UPLOAD_DIR} || true
            curl -u uploader:$REPO_PASS \
              -X PUT --upload-file ${artifactPath} \
              ${env.UPLOAD_URL}
          """
        }
      }
    }
  }

  post {
    always {
      archiveArtifacts artifacts: "${env.PROJECT_DIR}/.pio/build/esp32dev/${env.ARTIFACT}", allowEmptyArchive: true
    }
  }
}
